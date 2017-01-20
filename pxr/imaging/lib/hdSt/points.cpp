//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/imaging/glf/glew.h"

#include "pxr/imaging/hdSt/points.h"
#include "pxr/imaging/hdSt/pointsShaderKey.h"

#include "pxr/base/gf/vec2i.h"
#include "pxr/base/tf/getenv.h"

#include "pxr/imaging/hd/bufferSource.h"
#include "pxr/imaging/hd/geometricShader.h"
#include "pxr/imaging/hd/perfLog.h"
#include "pxr/imaging/hd/repr.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/vtBufferSource.h"
#include "pxr/base/vt/value.h"

// static repr configuration
HdStPoints::_PointsReprConfig HdStPoints::_reprDescConfig;

HdStPoints::HdStPoints(HdSceneDelegate* delegate, SdfPath const& id,
                       SdfPath const& instancerId)
  : HdPoints(delegate, id, instancerId)
{
    /*NOTHING*/
}

HdStPoints::~HdStPoints()
{
    /*NOTHING*/
}

void
HdStPoints::_UpdateDrawItem(HdDrawItem *drawItem,
                            HdChangeTracker::DirtyBits *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();

    /* VISIBILITY */
    _UpdateVisibility(dirtyBits);

    /* CONSTANT PRIMVARS, TRANSFORM AND EXTENT */
    _PopulateConstantPrimVars(drawItem, dirtyBits);

    /* INSTANCE PRIMVARS */
    _PopulateInstancePrimVars(drawItem, dirtyBits, InstancePrimVar);

    HdSt_PointsShaderKey shaderKey;
    drawItem->SetGeometricShader(Hd_GeometricShader::Create(shaderKey));

    /* PRIMVAR */
    if (HdChangeTracker::IsAnyPrimVarDirty(*dirtyBits, id)) {
        _PopulateVertexPrimVars(drawItem, dirtyBits);
    }

    // VertexPrimVar may be null, if there are no points in the prim.

    TF_VERIFY(drawItem->GetConstantPrimVarRange());
}

/* static */
void
HdStPoints::ConfigureRepr(TfToken const &reprName,
                          const HdStPointsReprDesc &desc)
{
    HD_TRACE_FUNCTION();

    _reprDescConfig.Append(reprName, _PointsReprConfig::DescArray{desc});
}

HdReprSharedPtr const &
HdStPoints::_GetRepr(TfToken const &reprName,
                     HdChangeTracker::DirtyBits *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    _PointsReprConfig::DescArray descs = _reprDescConfig.Find(reprName);

    _ReprVector::iterator it = _reprs.begin();
    bool isNew = it == _reprs.end();
    if (isNew) {
        it = _reprs.insert(_reprs.end(),
                           std::make_pair(reprName,
                                          HdReprSharedPtr(new HdRepr())));

        // allocate all draw items
        for (auto desc : descs) {
            if (desc.geomStyle == HdPointsGeomStyleInvalid) {
                continue;
            }
            it->second->AddDrawItem(&_sharedData);
        }
    }

    // points don't have multiple draw items (for now)
    if (isNew || HdChangeTracker::IsDirty(*dirtyBits)) {
        if (descs[0].geomStyle != HdPointsGeomStyleInvalid) {
            _UpdateDrawItem(it->second->GetDrawItem(0), dirtyBits);
        }
    }

    return it->second;
}

void
HdStPoints::_PopulateVertexPrimVars(HdDrawItem *drawItem,
                                    HdChangeTracker::DirtyBits *dirtyBits)
{
    HD_TRACE_FUNCTION();
    HF_MALLOC_TAG_FUNCTION();

    SdfPath const& id = GetId();
    HdResourceRegistry *resourceRegistry = &HdResourceRegistry::GetInstance();

    // The "points" attribute is expected to be in this list.
    TfTokenVector primVarNames = GetPrimVarVertexNames();
    TfTokenVector const& vars = GetPrimVarVaryingNames();
    primVarNames.insert(primVarNames.end(), vars.begin(), vars.end());

    HdBufferSourceVector sources;
    sources.reserve(primVarNames.size());

    int pointsIndexInSourceArray = -1;

    TF_FOR_ALL(nameIt, primVarNames) {
        if (!HdChangeTracker::IsPrimVarDirty(*dirtyBits, id, *nameIt))
            continue;

        // TODO: We don't need to pull primvar metadata every time a value
        // changes, but we need support from the delegate.

        //assert name not in range.bufferArray.GetResources()
        VtValue value = GetPrimVar(*nameIt);

        if (!value.IsEmpty()) {
            // Store where the points will be stored in the source array
            // we need this later to figure out if the number of points is changing
            // and we need to force a garbage collection to resize the buffer
            if (*nameIt == HdTokens->points) {
                pointsIndexInSourceArray = sources.size();
            }

            // XXX: do we need special treatment for width as basicCurves?

            HdBufferSourceSharedPtr source(new HdVtBufferSource(*nameIt, value));
            sources.push_back(source);
        }
    }

    // return before allocation if it's empty.
    if (sources.empty())
        return;

    if (!drawItem->GetVertexPrimVarRange() ||
        !drawItem->GetVertexPrimVarRange()->IsValid()) {
        // initialize buffer array
        HdBufferSpecVector bufferSpecs;
        TF_FOR_ALL(it, sources) {
            (*it)->AddBufferSpecs(&bufferSpecs);
        }

        HdBufferArrayRangeSharedPtr range =
            resourceRegistry->AllocateNonUniformBufferArrayRange(
                HdTokens->primVar, bufferSpecs);
        _sharedData.barContainer.Set(
            drawItem->GetDrawingCoord()->GetVertexPrimVarIndex(), range);

    } else if (pointsIndexInSourceArray >=0) {

        int previousRange = drawItem->GetVertexPrimVarRange()->GetNumElements();
        int newRange = sources[pointsIndexInSourceArray]->GetNumElements();

        // Check if the range is different and if so force a garbage collection
        // which will make sure the points are up to date
        if(previousRange != newRange) {
            _GetRenderIndex().GetChangeTracker().SetGarbageCollectionNeeded();
        }
    }

    // add sources to update queue
    resourceRegistry->AddSources(drawItem->GetVertexPrimVarRange(),
                                 sources);
}

HdChangeTracker::DirtyBits 
HdStPoints::_GetInitialDirtyBits() const
{
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::DirtyExtent
        | HdChangeTracker::DirtyInstanceIndex
        | HdChangeTracker::DirtyPoints
        | HdChangeTracker::DirtyPrimID
        | HdChangeTracker::DirtyPrimVar
        | HdChangeTracker::DirtyRepr
        | HdChangeTracker::DirtySurfaceShader
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyWidths
        ;

    return (HdChangeTracker::DirtyBits)mask;
}
