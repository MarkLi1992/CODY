/**
 * Copyright (c) 2016-2017 Los Alamos National Security, LLC
 *                         All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * LA-CC 10-123
 */

//@HEADER
// ***************************************************
//
// HPCG: High Performance Conjugate Gradient Benchmark
//
// Contact:
// Michael A. Heroux ( maherou@sandia.gov)
// Jack Dongarra     (dongarra@eecs.utk.edu)
// Piotr Luszczek    (luszczek@eecs.utk.edu)
//
// ***************************************************
//@HEADER

#include "hpcg.hpp"
#include "mytimer.hpp"
#include "Geometry.hpp"
#include "CheckAspectRatio.hpp"
#include "GenerateGeometry.hpp"
#include "GenerateProblem.hpp"
#include "VectorOps.hpp"
#if 0
#include "SetupHalo.hpp"
#endif
#include "CG.hpp"

#include "LegionStuff.hpp"
#include "LegionArrays.hpp"
#include "LegionMatrices.hpp"
#include "LegionCGData.hpp"

#include <iostream>
#include <cstdlib>
#include <iomanip>

#include <unistd.h>

using namespace std;

LegionRuntime::Logger::Category Logger("LGNCG");

using namespace LegionRuntime::HighLevel;

/**
 * Generate Problem Task ///////////////////////////////////////////////////////
 */
void
genProblemTask(
    const Task *task,
    const std::vector<PhysicalRegion> &regions,
    Context ctx, HighLevelRuntime *runtime
) {
    const int taskID = getTaskID(task);
    //
    HPCG_Params params = *(HPCG_Params *)task->args;
    // Number of processes, my process ID
    int size = params.comm_size, rank = taskID;
    //
    local_int_t nx, ny,nz;
    nx = (local_int_t)params.nx;
    ny = (local_int_t)params.ny;
    nz = (local_int_t)params.nz;
    // Used to check return codes on function calls
    int ierr = 0;
    ierr = CheckAspectRatio(0.125, nx, ny, nz, "local problem", rank == 0);
    if (ierr) exit(ierr);
    ////////////////////////////////////////////////////////////////////////////
    // Problem setup Phase /////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////
    // Construct the geometry and linear system
    size_t rid = 0;
    //
    SparseMatrix A(regions, rid, ctx, runtime);
    rid += A.nRegionEntries();
    Array<floatType> b     (regions[rid++], ctx, runtime);
    Array<floatType> x     (regions[rid++], ctx, runtime);
    Array<floatType> xexact(regions[rid++], ctx, runtime);
    //
    Geometry *geom = A.geom->data();
    GenerateGeometry(size, rank, params.numThreads,
                     nx, ny, nz, params.stencilSize, geom);
    //
    ierr = CheckAspectRatio(0.125, geom->npx, geom->npy, geom->npz,
                            "process grid", rank == 0);
    if (ierr) exit(ierr);
    // Use this array for collecting timing information
    std::vector<double> times(10, 0.0);
    //
    GenerateProblem(A, &b, &x, &xexact, ctx, runtime);
}

/**
 *
 */
static void
generateInitGeometry(
    int nShards,
    const HPCG_Params &params,
    Geometry &globalGeom
) {
    // Number of processes, my process ID
    const int size = params.comm_size, rank = 0;
    //
    local_int_t nx, ny,nz;
    nx = (local_int_t)params.nx;
    ny = (local_int_t)params.ny;
    nz = (local_int_t)params.nz;
    // Generate geometry so we can get things like npx, npy, npz, etc.
    GenerateGeometry(size, rank, params.numThreads,
                     nx, ny, nz, params.stencilSize, &globalGeom);
}

/**
 *
 */
static void
createLogicalStructures(
    LogicalSparseMatrix     &A,
    LogicalArray<floatType> &x,
    LogicalArray<floatType> &y,
    LogicalArray<floatType> &xexact,
    const Geometry          &geom,
    Context ctx, HighLevelRuntime *runtime
) {
    cout << "*** Creating Logical Structures..." << endl;
    const double initStart = mytimer();
    // Application structures
    // First calculate global XYZ for the problem.
    global_int_t globalXYZ = getGlobalXYZ(geom);
    //
    A.allocate(geom, ctx, runtime);
    A.partition(geom.size, ctx, runtime);
    x.allocate(globalXYZ, ctx, runtime);
    x.partition(geom.size, ctx, runtime);
    y.allocate(globalXYZ, ctx, runtime);
    y.partition(geom.size, ctx, runtime);
    xexact.allocate(globalXYZ, ctx, runtime);
    xexact.partition(geom.size, ctx, runtime);
    const double initEnd = mytimer();
    const double initTime = initEnd - initStart;
    cout << "--> Time=" << initTime << "s" << endl;
}

/**
 *
 */
static void
destroyLogicalStructures(
    LogicalSparseMatrix     &A,
    LogicalArray<floatType> &x,
    LogicalArray<floatType> &y,
    LogicalArray<floatType> &xexact,
    LogicalCGData           &data,
    Context ctx, HighLevelRuntime *runtime
) {
    cout << "*** Destroying Logical Structures..." << endl;
    const double initStart = mytimer();
    A.deallocate(ctx, runtime);
    x.deallocate(ctx, runtime);
    y.deallocate(ctx, runtime);
    xexact.deallocate(ctx, runtime);
    data.deallocate(ctx, runtime);
    const double initEnd = mytimer();
    const double initTime = initEnd - initStart;
    cout << "--> Time=" << initTime << "s" << endl;
}

/**
 * Main Task ///////////////////////////////////////////////////////////////////
 * First task that gets spawned. Responsible for setup, etc.
 */
void
mainTask(
    const Task *,
    const std::vector<PhysicalRegion> &,
    Context ctx, HighLevelRuntime *runtime
) {
    // Ask the mapper how many shards we can have.
    const size_t nShards = getNumProcs();
    // TODO FIXME
    assert(nShards > 1 && "Run with at least 2 shards (e.g., try -ll:cpu 2)");
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////
    // At this point we need to know some run parameters so we can allocate and
    // partition the logical data structures. We'll use this info to calculate
    // global values, etc. NOTE: not all members will contain valid data after
    // generateInitGeometry returns (e.g., rank, ipx, ipy, ipz).
    cout << "*** Number of Shards (~ NUMPE)=" << nShards << endl;;
    //
    // We only care about passing nShards for this bit. rank doesn't make sense
    // in this context.
    const SPMDMeta meta = {.rank = 0, .nRanks = int(nShards)};
    HPCG_Params params;
    HPCG_Init(params, meta);
    //
    Geometry initGeom;
    generateInitGeometry(nShards, params, initGeom);
    cout << "*** Problem Information:"   << endl;
    cout << "--> size=" << initGeom.size << endl;
    cout << "--> npx="  << initGeom.npx  << endl;
    cout << "--> npy="  << initGeom.npy  << endl;
    cout << "--> npz="  << initGeom.npz  << endl;
    cout << "--> nx="   << initGeom.nx   << endl;
    cout << "--> ny="   << initGeom.ny   << endl;
    cout << "--> nz="   << initGeom.nz   << endl;
    ////////////////////////////////////////////////////////////////////////////
    cout << "*** Starting Initialization..." << endl;;
    // Application structures.
    LogicalSparseMatrix A;
    LogicalArray<floatType> b, x, xexact;
    //
    createLogicalStructures(
        A,
        b,
        x,
        xexact,
        initGeom,
        ctx,
        runtime
    );
    {
        //
        cout << "*** Launching Initialization Tasks..." << endl;;
        const double initStart = mytimer();
        IndexLauncher launcher(
            GEN_PROB_TID,
            A.launchDomain,
            TaskArgument(&params, sizeof(params)),
            ArgumentMap()
        );
        A.intent(WO_E, launcher);
        b.intent(WO_E, launcher);
        x.intent(WO_E, launcher);
        xexact.intent(WO_E, launcher);
        //
        auto futureMap = runtime->execute_index_space(ctx, launcher);
        cout << "*** Waiting for Initialization Tasks" << endl;
        futureMap.wait_all_results();
        const double initEnd = mytimer();
        double initTime = initEnd - initStart;
        cout << "--> Time=" << initTime << "s" << endl;
    }
    {
        cout << "*** Calculating total number of non-zeros..." << endl;;
        const double initStart = mytimer();

        Array<SparseMatrixScalars> sms(A.sclrs.mapRegion(RW_E, ctx, runtime),
                                       ctx, runtime);
        SparseMatrixScalars *Asclrs = sms.data();

        global_int_t total = 0;
        for (int i = 0; i < nShards; ++i) {
            total += Asclrs[i].localNumberOfNonzeros;
        }
        // Update values in mapped regions.
        for (int i = 0; i < nShards; ++i) {
            Asclrs[i].totalNumberOfNonzeros = total;
        }
        // Done. Unmap region.
        A.sclrs.unmapRegion(ctx, runtime);

        // If this assert fails, it most likely means that the global_int_t is
        // set to int and should be set to long long This assert is usually the
        // first to fail as problem size increases beyond the 32-bit integer
        // range.  Throw an exception of the number of nonzeros is less than
        // zero (can happen if int overflow)
        assert(total > 0);

        cout << "--> totalNumberOfNonzeros=" << total << endl;;

        const double initEnd = mytimer();
        double initTime = initEnd - initStart;
        cout << "--> Time=" << initTime << "s" << endl;
    }
#if 0
    // Now that we have all the setup information stored in LogicalRegions,
    // perform the top-level setup required for inter-task synchronization using
    // PhaseBarriers.
    SetupHaloTopLevel(A, initGeom, ctx, runtime);
#endif

    LogicalCGData data;
    data.allocate(initGeom, ctx, runtime);
    data.partition(initGeom.size, ctx, runtime);

    ////////////////////////////////////////////////////////////////////////////
    // Launch the tasks to begin the solve.
    ////////////////////////////////////////////////////////////////////////////
    {
        cout << "*** Starting Solve..." << endl;
        IndexLauncher launcher(
            START_SOLVE,
            A.launchDomain,
            TaskArgument(&params, sizeof(params)),
            ArgumentMap()
        );
        A.intent(RW_E, launcher);
        b.intent(RW_E, launcher);
        x.intent(RW_E, launcher);
        xexact.intent(RW_E, launcher);
        data.intent(RW_E, launcher);
        //
        MustEpochLauncher mel;
        mel.add_index_task(launcher);
        FutureMap fm = runtime->execute_must_epoch(ctx, mel);
        fm.wait_all_results();
    }
    //
    cout << "*** Cleaning Up..." << endl;
    destroyLogicalStructures(
        A,
        b,
        x,
        xexact,
        data,
        ctx,
        runtime
    );
}

/**
 *
 */
void
startSolveTask(
    const Task *task,
    const std::vector<PhysicalRegion> &regions,
    Context ctx, HighLevelRuntime *lrt
) {
    HPCG_Params params = *(HPCG_Params *)task->args;
    //
    size_t rid = 0;
    SparseMatrix     A     (regions, rid, ctx, lrt);
    rid += A.nRegionEntries();
    Array<floatType> b     (regions[rid++], ctx, lrt);
    Array<floatType> x     (regions[rid++], ctx, lrt);
    Array<floatType> xexact(regions[rid++], ctx, lrt);
    CGData           data  (regions, rid,  ctx, lrt);
    rid += data.nRegionEntries();

    const int refMaxIters  = 50;
    const int optMaxIters  = 10 * refMaxIters;
    //
#if 0 // FIXME
    int numberOfCalls = 10;
#else
    int numberOfCalls = 1;
#endif
    // Check if QuickPath option is enabled.  If the running time is set to
    // zero, we minimize all paths through the program
    bool quickPath = (params.runningTime == 0);
    //QuickPath means we do on one call of each block of repetitive code
    if (quickPath) numberOfCalls = 1;
    //
    int niters          = 0;
    double normr        = 0.0;
    double normr0       = 0.0;
    int errCount        = 0;
    double optWorstTime = 0.0;
    // Set tolerance to zero to make all runs do maxIters iterations
    double tolerance    = 0.0;

    std::vector<double> optTimes(9, 0.0);
    // Compute the residual reduction and residual count for the user ordering
    // and optimized kernels.
    for (int i = 0; i < numberOfCalls; ++i) {
        ZeroVector(x); // start x at all zeros
        double lastCummulativeTime = optTimes[0];
        int ierr = CG(A,
                      data,
                      b,
                      x,
                      optMaxIters,
                      tolerance,
                      niters,
                      normr,
                      normr0,
                      &optTimes[0],
                      false // TODO FIXME
                   );
        if (ierr) ++errCount; // count the number of errors in CG
#if 0
        // the number of failures to reduce residual
        if (normr / normr0 > refTolerance) ++toleranceFailures;
        // pick the largest number of iterations to guarantee convergence
        if (niters > optNiters) optNiters = niters;
#endif
        double current_time = optTimes[0] - lastCummulativeTime;
        if (current_time > optWorstTime) optWorstTime = current_time;
    }
}

/**
 * Main ////////////////////////////////////////////////////////////////////////
 * Responsible for RT initialization.
 */
int
main(int argc, char **argv)
{
    LegionInit();
    return Runtime::start(argc, argv);
}