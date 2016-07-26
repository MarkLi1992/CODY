/*
 * Copyright (c) 2014-2016 Los Alamos National Security, LLC
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
 */

#pragma once

#include "LegionItems.hpp"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
template<typename TYPE>
struct LogicalArray : public LogicalItem<TYPE> {
public:
    /**
     *
     */
    LogicalArray(void) : LogicalItem<TYPE>() { }

    /**
     *
     */
    void
    allocate(
        int64_t nElems,
        LegionRuntime::HighLevel::Context &ctx,
        LegionRuntime::HighLevel::HighLevelRuntime *lrt
    ) {
        this->mAllocate(nElems, ctx, lrt);
    }
    /**
     * Returns whether or not two LogicalArrays are the same (as far as the
     * Legion RT is concerned).
     */
    static bool
    same(
        const LogicalArray &a,
        const LogicalArray &b
    ) {
        return a.mIndexSpaceID == b.mIndexSpaceID &&
               a.mFieldSpaceID == b.mFieldSpaceID &&
               a.mRTreeID      == b.mRTreeID;
    }

    /**
     *
     */
    void
    partition(
        int64_t nParts,
        LegionRuntime::HighLevel::Context &ctx,
        LegionRuntime::HighLevel::HighLevelRuntime *lrt
    ) {
        using namespace LegionRuntime::HighLevel;
        using LegionRuntime::Arrays::Rect;

        // For now only allow even partitioning.
        assert(0 == this->mLength % nParts && "Uneven partitioning requested.");
        //
        int64_t inc = this->mLength / nParts; // the increment
        Rect<1> colorBounds(Point<1>(0), Point<1>(nParts - 1));
        Domain colorDomain = Domain::from_rect<1>(colorBounds);
        //          +
        //          |
        //          |
        //     (x1)-+-+
        //          | |
        //          | m / nSubregions
        //     (x0) + |
        int64_t x0 = 0, x1 = inc - 1;
        DomainColoring disjointColoring;
        // a list of sub-grid bounds.
        // provides a task ID to sub-grid bounds mapping.
        std::vector< Rect<1> > subGridBounds;
        for (int64_t color = 0; color < nParts; ++color) {
            Rect<1> subRect((Point<1>(x0)), (Point<1>(x1)));
            // cache the subgrid bounds
            subGridBounds.push_back(subRect);
#if 0 // nice debug
            printf("vec disjoint partition: (%d) to (%d)\n",
                    subRect.lo.x[0], subRect.hi.x[0]);
#endif
            disjointColoring[color] = Domain::from_rect<1>(subRect);
            x0 += inc;
            x1 += inc;
        }
        auto iPart = lrt->create_index_partition(
                         ctx, this->mIndexSpace,
                         colorDomain, disjointColoring,
                         true /* disjoint */
        );
        // logical partitions
        using LegionRuntime::HighLevel::LogicalPartition;
        this->logicalPartition = lrt->get_logical_partition(
                                     ctx, this->logicalRegion, iPart
                                 );
        // launch domain -- one task per color
        // launch domain
        this->launchDomain = colorDomain;
    }
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
template<typename TYPE>
class Array : public Item<TYPE> {
public:
    /**
     *
     */
    Array(void) = default;

    /**
     *
     */
    Array(
        const PhysicalRegion &physicalRegion,
        Context ctx,
        HighLevelRuntime *runtime
    ) : Item<TYPE>(physicalRegion, ctx, runtime) { }

    /**
     *
     */
    size_t
    length(void) { return this->mLength; }
};

/**
 *
 */
template<
    LegionRuntime::HighLevel::PrivilegeMode PRIV_MODE,
    LegionRuntime::HighLevel::CoherenceProperty COH_PROP
>
static void
intent(
    LegionRuntime::HighLevel::IndexLauncher &launcher,
    const std::deque<LogicalItemBase> &targetArrays
) {
    for (auto &a : targetArrays) {
        launcher.add_region_requirement(
            RegionRequirement(
                a.logicalPartition,
                0,
                PRIV_MODE,
                COH_PROP,
                a.logicalRegion
            )
        ).add_field(a.fid);
    }
}
