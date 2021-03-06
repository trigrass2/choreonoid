/*!
  @file
  @author Shin'ichiro Nakaoka
*/

#include "SceneGraph.h"
#include "SceneVisitor.h"
#include "Exception.h"
#include <unordered_map>

using namespace std;
using namespace cnoid;


SgUpdate::~SgUpdate()
{

}

namespace {
typedef std::unordered_map<const SgObject*, SgObjectPtr> CloneMap;
}

namespace cnoid {
class SgCloneMapImpl : public CloneMap { };
}


SgCloneMap::SgCloneMap()
{
    cloneMap = new SgCloneMapImpl;
    isNonNodeCloningEnabled_ = true;
}


SgCloneMap::SgCloneMap(const SgCloneMap& org)
{
    cloneMap = new SgCloneMapImpl(*org.cloneMap);
    isNonNodeCloningEnabled_ = org.isNonNodeCloningEnabled_;
}


void SgCloneMap::clear()
{
    cloneMap->clear();
}


SgObject* SgCloneMap::findOrCreateClone(const SgObject* org)
{
    CloneMap::iterator p = cloneMap->find(org);
    if(p == cloneMap->end()){
        SgObject* clone = org->clone(*this);
        (*cloneMap)[org] = clone;
        return clone;
    } else {
        return p->second.get();
    }
}


SgCloneMap::~SgCloneMap()
{
    delete cloneMap;
}


SgObject::SgObject()
{

}


SgObject::SgObject(const SgObject& org)
    : name_(org.name_)
{

}


SgObject* SgObject::clone(SgCloneMap& cloneMap) const
{
    return new SgObject(*this);
}


int SgObject::numChildObjects() const
{
    return 0;
}


SgObject* SgObject::childObject(int index)
{
    return 0;
}


void SgObject::onUpdated(SgUpdate& update)
{
    update.push(this);
    sigUpdated_(update);
    for(const_parentIter p = parents.begin(); p != parents.end(); ++p){
        (*p)->onUpdated(update);
    }
    update.pop();
}


void SgObject::addParent(SgObject* parent, bool doNotify)
{
    parents.insert(parent);
    if(doNotify){
        SgUpdate update(SgUpdate::ADDED);
        update.push(this);
        parent->onUpdated(update);
    }
    if(parents.size() == 1){
        sigGraphConnection_(true);
    }
}


void SgObject::removeParent(SgObject* parent)
{
    parents.erase(parent);
    if(parents.empty()){
        sigGraphConnection_(false);
    }
}


SgNode::SgNode()
{

}


SgNode::SgNode(const SgNode& org)
    : SgObject(org)
{

}


SgNode::~SgNode()
{

}


SgObject* SgNode::clone(SgCloneMap& cloneMap) const
{
    return new SgNode(*this);
}


void SgNode::accept(SceneVisitor& visitor)
{
    visitor.visitNode(this);
}


const BoundingBox& SgNode::boundingBox() const
{
    static const BoundingBox bbox; // empty one
    return bbox;
}


bool SgNode::isGroup() const
{
    return false;
}


SgGroup::SgGroup()
{
    isBboxCacheValid = false;
}


SgGroup::SgGroup(const SgGroup& org)
    : SgNode(org)
{
    children.reserve(org.numChildren());

    // shallow copy
    for(const_iterator p = org.begin(); p != org.end(); ++p){
        addChild(*p, false);
    }

    isBboxCacheValid = true;
    bboxCache = org.bboxCache;
}


SgGroup::SgGroup(const SgGroup& org, SgCloneMap& cloneMap)
    : SgNode(org)
{
    children.reserve(org.numChildren());

    for(const_iterator p = org.begin(); p != org.end(); ++p){
        addChild(cloneMap.getClone<SgNode>(p->get()), false);
    }

    isBboxCacheValid = true;
    bboxCache = org.bboxCache;
}


SgGroup::~SgGroup()
{
    for(const_iterator p = begin(); p != end(); ++p){
        (*p)->removeParent(this);
    }
}


SgObject* SgGroup::clone(SgCloneMap& cloneMap) const
{
    return new SgGroup(*this, cloneMap);
}


int SgGroup::numChildObjects() const
{
    return children.size();
}


SgObject* SgGroup::childObject(int index)
{
    return children[index].get();
}


void SgGroup::accept(SceneVisitor& visitor)
{
    visitor.visitGroup(this);
}


void SgGroup::onUpdated(SgUpdate& update)
{
    //if(update.action() & SgUpdate::BBOX_UPDATED){
    invalidateBoundingBox();
    SgNode::onUpdated(update);
    //}
}


const BoundingBox& SgGroup::boundingBox() const
{
    if(isBboxCacheValid){
        return bboxCache;
    }
    bboxCache.clear();
    for(const_iterator p = begin(); p != end(); ++p){
        bboxCache.expandBy((*p)->boundingBox());
    }
    isBboxCacheValid = true;

    return bboxCache;
}


bool SgGroup::isGroup() const
{
    return true;
}


bool SgGroup::contains(SgNode* node) const
{
    for(const_iterator p = begin(); p != end(); ++p){
        if((*p) == node){
            return true;
        }
    }
    return false;
}


void SgGroup::addChild(SgNode* node, bool doNotify)
{
    if(node){
        children.push_back(node);
        node->addParent(this, doNotify);
    }
}


bool SgGroup::addChildOnce(SgNode* node, bool doNotify)
{
    if(!contains(node)){
        addChild(node, doNotify);
        return true;
    }
    return false;
}


void SgGroup::insertChild(SgNode* node, int index, bool doNotify)
{
    if(node){
        if(index > children.size()){
            index = children.size();
        }
        children.insert(children.begin() + index, node);
        node->addParent(this, doNotify);
    }
}


SgGroup::iterator SgGroup::removeChild(iterator childIter, bool doNotify)
{
    iterator next;
    SgNode* child = *childIter;
    child->removeParent(this);
    
    if(!doNotify){
        next = children.erase(childIter);
    } else {
        SgNodePtr childHolder = child;
        next = children.erase(childIter);
        SgUpdate update(SgUpdate::REMOVED);
        update.push(child);
        onUpdated(update);
    }
    return next;
}


bool SgGroup::removeChild(SgNode* node, bool doNotify)
{
    bool removed = false;
    iterator p = children.begin();
    while(p != children.end()){
        if((*p) == node){
            p = removeChild(p, doNotify);
            removed = true;
        } else {
            ++p;
        }
    }
    return removed;
}


void SgGroup::removeChildAt(int index, bool doNotify)
{
    removeChild(children.begin() + index, doNotify);
}


void SgGroup::clearChildren(bool doNotify)
{
    iterator p = children.begin();
    while(p != children.end()){
        p = removeChild(p, doNotify);
    }
}


void SgGroup::copyChildrenTo(SgGroup* group, bool doNotify)
{
    for(int i=0; i < children.size(); ++i){
        group->addChild(child(i), doNotify);
    }
}


void SgGroup::moveChildrenTo(SgGroup* group, bool doNotify)
{
    const int destTop = group->children.size();
    
    for(int i=0; i < children.size(); ++i){
        group->addChild(child(i));
    }
    clearChildren(doNotify);
    if(doNotify){
        SgUpdate update(SgUpdate::ADDED);
        for(int i=destTop; i < group->numChildren(); ++i){
            group->child(i)->notifyUpdate(update);
        }
    }
}


void SgGroup::throwTypeMismatchError()
{
    throw type_mismatch_error();
}


SgInvariantGroup::SgInvariantGroup()
{

}


SgInvariantGroup::SgInvariantGroup(const SgInvariantGroup& org)
    : SgGroup(org)
{

}


SgInvariantGroup::SgInvariantGroup(const SgInvariantGroup& org, SgCloneMap& cloneMap)
    : SgGroup(org, cloneMap)
{

}


SgObject* SgInvariantGroup::clone(SgCloneMap& cloneMap) const
{
    return new SgInvariantGroup(*this, cloneMap);
}


void SgInvariantGroup::accept(SceneVisitor& visitor)
{
    visitor.visitInvariantGroup(this);
}


SgTransform::SgTransform()
{

}


SgTransform::SgTransform(const SgTransform& org)
    : SgGroup(org)
{
    untransformedBboxCache = org.untransformedBboxCache;
}
    

SgTransform::SgTransform(const SgTransform& org, SgCloneMap& cloneMap)
    : SgGroup(org, cloneMap)
{
    untransformedBboxCache = org.untransformedBboxCache;
}


const BoundingBox& SgTransform::untransformedBoundingBox() const
{
    if(!isBboxCacheValid){
        boundingBox();
    }
    return untransformedBboxCache;
}


SgPosTransform::SgPosTransform()
    : T_(Affine3::Identity())
{

}


SgPosTransform::SgPosTransform(const Affine3& T)
    : T_(T)
{

}


SgPosTransform::SgPosTransform(const SgPosTransform& org)
    : SgTransform(org),
      T_(org.T_)
{

}


SgPosTransform::SgPosTransform(const SgPosTransform& org, SgCloneMap& cloneMap)
    : SgTransform(org, cloneMap),
      T_(org.T_)
{

}


SgObject* SgPosTransform::clone(SgCloneMap& cloneMap) const
{
    return new SgPosTransform(*this, cloneMap);
}


void SgPosTransform::accept(SceneVisitor& visitor)
{
    visitor.visitPosTransform(this);
}


const BoundingBox& SgPosTransform::boundingBox() const
{
    if(isBboxCacheValid){
        return bboxCache;
    }
    bboxCache.clear();
    for(const_iterator p = begin(); p != end(); ++p){
        bboxCache.expandBy((*p)->boundingBox());
    }
    untransformedBboxCache = bboxCache;
    bboxCache.transform(T_);
    isBboxCacheValid = true;
    return bboxCache;
}


void SgPosTransform::getTransform(Affine3& out_T) const
{
    out_T = T_;
}


SgScaleTransform::SgScaleTransform()
{
    scale_.setOnes();
}


SgScaleTransform::SgScaleTransform(const SgScaleTransform& org)
    : SgTransform(org),
      scale_(org.scale_)
{

}
      
    
SgScaleTransform::SgScaleTransform(const SgScaleTransform& org, SgCloneMap& cloneMap)
    : SgTransform(org, cloneMap),
      scale_(org.scale_)
{

}


SgObject* SgScaleTransform::clone(SgCloneMap& cloneMap) const
{
    return new SgScaleTransform(*this, cloneMap);
}


void SgScaleTransform::accept(SceneVisitor& visitor)
{
    visitor.visitScaleTransform(this);
}


const BoundingBox& SgScaleTransform::boundingBox() const
{
    if(isBboxCacheValid){
        return bboxCache;
    }
    bboxCache.clear();
    for(const_iterator p = begin(); p != end(); ++p){
        bboxCache.expandBy((*p)->boundingBox());
    }
    untransformedBboxCache = bboxCache;
    bboxCache.transform(Affine3(scale_.asDiagonal()));
    isBboxCacheValid = true;
    return bboxCache;
}


void SgScaleTransform::getTransform(Affine3& out_T) const
{
    out_T = scale_.asDiagonal();
}


SgSwitch::SgSwitch()
{
    isTurnedOn_ = true;
}


SgSwitch::SgSwitch(const SgSwitch& org)
    : SgGroup(org)
{
    isTurnedOn_ = org.isTurnedOn_;
}


SgSwitch::SgSwitch(const SgSwitch& org, SgCloneMap& cloneMap)
    : SgGroup(org, cloneMap)
{
    isTurnedOn_ = org.isTurnedOn_;
}


SgObject* SgSwitch::clone(SgCloneMap& cloneMap) const
{
    return new SgSwitch(*this, cloneMap);
}


void SgSwitch::accept(SceneVisitor& visitor)
{
    visitor.visitSwitch(this);
}


SgUnpickableGroup::SgUnpickableGroup()
{

}


SgUnpickableGroup::SgUnpickableGroup(const SgUnpickableGroup& org)
    : SgGroup(org)
{

}


SgUnpickableGroup::SgUnpickableGroup(const SgUnpickableGroup& org, SgCloneMap& cloneMap)
    : SgGroup(org, cloneMap)
{

}


SgObject* SgUnpickableGroup::clone(SgCloneMap& cloneMap) const
{
    return new SgUnpickableGroup(*this, cloneMap);
}


void SgUnpickableGroup::accept(SceneVisitor& visitor)
{
    visitor.visitUnpickableGroup(this);
}


SgPreprocessed::SgPreprocessed()
{

}


SgPreprocessed::SgPreprocessed(const SgPreprocessed& org)
    : SgNode(org)
{

}


SgObject* SgPreprocessed::clone(SgCloneMap& cloneMap) const
{
    return new SgPreprocessed(*this);
}


void SgPreprocessed::accept(SceneVisitor& visitor)
{
    visitor.visitPreprocessed(this);
}
