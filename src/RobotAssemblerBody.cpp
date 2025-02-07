#include "RobotAssemblerBody.h"
#include "RobotAssemblerInfo.h"
// shape
#include <cnoid/SceneLoader>
#include <cnoid/MeshGenerator>
#include <cnoid/CloneMap>

#include <cnoid/UTF8>
#include <cnoid/stdx/filesystem>

//#define IRSL_DEBUG
#include "irsl_debug.h"

namespace filesystem = cnoid::stdx::filesystem;

namespace cnoid {
namespace robot_assembler {

const Vector3f default_body_color(0.0f, 0.0f, 0.8f);
const Vector3f default_mesh_color(0.95f, 0.95f, 0.95f);

static inline void addMaterial(SgNode *_nd, const Vector3f &_color, float _intensity = 0.7)
{
    SgShape *_shape = dynamic_cast<SgShape *>(_nd);
    if(!_shape) return;
    SgMaterialPtr mat_(new SgMaterial());
    //mat_->setName("material");
    //Vector3f color(0.1f, 0.1f, 0.7f);
    mat_->setDiffuseColor(_color);
    mat_->setAmbientIntensity(_intensity);
    mat_->setEmissiveColor(Vector3f(0.0f, 0.0f, 0.0f));
    _shape->setMaterial(mat_);
}
void createSceneFromGeometry(SgGroup *sg_main, std::vector<Geometry> &geom_list, const Vector3f &_color) {
    createSceneFromGeometry(sg_main, geom_list, std::string(), _color);
}
void createSceneFromGeometry(SgGroup *sg_main, std::vector<Geometry> &geom_list,
                             const std::string &_proj_dir, const Vector3f &_color)
{
    DEBUG_PRINT();
    if (geom_list.size() <= 0) {
        //
        return;
    }
    const std::string &name_ = sg_main->name();
    for(int i = 0; i < geom_list.size(); i++) {
        Geometry &geom = geom_list[i];
        if (geom.type == Geometry::Mesh) {
            SceneLoader sceneLoader;
            sceneLoader.setMessageSink(std::cerr);
            std::string geom_file_path = geom.url;
            filesystem::path path_(fromUTF8(geom.url));
            if(_proj_dir.size() > 0 && path_.is_relative()) {
                geom_file_path = _proj_dir;
                geom_file_path = geom_file_path + "/" + geom.url;
            }
            DEBUG_STREAM(" mesh load: " << geom_file_path);
            SgNodePtr shape = sceneLoader.load(geom_file_path);
            if (!!shape) {
                DEBUG_STREAM(" mesh loaded!");
                shape->setName(name_ + "/geom");
                if(!_color.isZero()) {
                    addMaterial(shape, _color);
                } else if (!geom.color.isZero()) {
                    addMaterial(shape, geom.color);
                } else { // default color
                    addMaterial(shape, default_mesh_color);
                }
                Position p; geom.coords.toPosition(p);
                SgPosTransformPtr trs(new SgPosTransform(p));
                trs->setName(name_ + "/geom_postrans");
#define SCALE_EPS 0.005
                if ( geom.scale != 0.0 && ((geom.scale < 1.0 - SCALE_EPS) || (geom.scale > 1.0 + SCALE_EPS)) ) {
                    SgScaleTransformPtr scl_(new SgScaleTransform(geom.scale));
                    scl_->addChild(shape);
                    trs->addChild(scl_);
                    sg_main->addChild(trs);
                } else {
                    trs->addChild(shape);
                    sg_main->addChild(trs);
                }
            }
        } else if (geom.type == Geometry::Box) {
            // parameter
            MeshGenerator mg;
            SgMeshPtr mesh = mg.generateBox(Vector3(geom.parameter[0], geom.parameter[1], geom.parameter[2]));

            SgShapePtr shape(new SgShape());
            shape->setMesh(mesh);
            //shape->setName("box");
            // material
            if (!!shape) {
                shape->setName(name_ + "/box");
                if(!_color.isZero()) {
                    addMaterial(shape, _color);
                } else if (!geom.color.isZero()) {
                    addMaterial(shape, geom.color);
                } else {
                    addMaterial(shape, default_body_color);
                }
                Position p; geom.coords.toPosition(p);
                SgPosTransformPtr trs(new SgPosTransform(p));
                trs->setName(name_ + "/geom_postrans");
                trs->addChild(shape);
                sg_main->addChild(trs);
            }
        }
    }
}

RoboasmBodyCreator::RoboasmBodyCreator(const std::string &_proj_dir) : body(nullptr), joint_counter(0), merge_fixed_joint(false)
{
    project_directory = _proj_dir;
}
Link *RoboasmBodyCreator::createLink(RoboasmPartsPtr _pt, bool _is_root)
{
    Link *lk = new Link();
    coordinates link_origin_to_self_;
    cnoidRAInfo cinfo(info);

    if (_is_root) { // for root link
        std::string nm_("Root");
        cinfo.getPartsName(_pt->name(), nm_);
        lk->setName(nm_);
        //lk->setJointName();
        lk->setJointType(Link::FreeJoint);
        lk->setJointId(-1);
        // temp
        Position p;
        _pt->worldcoords().toPosition(p);
        lk->setOffsetPosition(p);
        //
        map_link_cnoid_roboasm.insert(std::pair<std::string, std::string>(nm_, _pt->name()));
    } else { // usual (has parent) link
        RoboasmPartsPtr p_pt_;
        RoboasmConnectingPointPtr p_cp_;
        RoboasmConnectingPointPtr s_cp_;
        if(!_pt->parentParts(p_pt_, p_cp_, s_cp_)) {
            ERROR_STREAM(" miss fetch : " << _pt->name());
            delete lk;
            return nullptr;
        }
        s_cp_->worldcoords().transformation(link_origin_to_self_,
                                            _pt->worldcoords());
        if(!p_pt_->parent()) {
            ERROR_STREAM(" invalid parts : " << p_pt_->name());
            delete lk;
            return nullptr;
        }
        coordinates cds;
        p_pt_->parent()->worldcoords().transformation(cds, s_cp_->worldcoords());
        Position p; cds.toPosition(p);
        lk->setOffsetPosition(p);
        RoboasmConnectingPointPtr act_;
        if (p_cp_->isActuator()) {
            act_ = p_cp_;
        } else if (s_cp_->isActuator()) {
            act_ = s_cp_;
        }
        if( !!act_ ) {
            // link-name
            std::ostringstream ss_;
            ss_ << "LINK_" << joint_counter;
            std::string lnm_ = ss_.str();
            cinfo.getPartsName(_pt->name(), lnm_);
            lk->setName(lnm_);
            // joint-name
            std::string jnm_ = lnm_;
            if(cinfo.getActuatorName(act_->name(), jnm_)) {
                lk->setJointName(jnm_);
            }
            DEBUG_STREAM(" _pt->name() : " << _pt->name());
            DEBUG_STREAM("act_->name() : " << act_->name());
            DEBUG_STREAM("        lnm_ : " << lnm_);
            DEBUG_STREAM("        jnm_ : " << jnm_);
            lk->setJointId(joint_counter++); // [todo] overwrite id by info
            map_link_cnoid_roboasm.insert(std::pair<std::string, std::string>(lnm_, _pt->name()));
            //map_joint_cnoid_roboasm.insert(std::pair<std::string, std::string>(jnm_, _pt->name()));
            Actuator *ainfo_ = dynamic_cast<Actuator*>(act_->info);
            if(!!ainfo_) {
                if (act_ == s_cp_) {
                    lk->setJointAxis(- ainfo_->axis); // [todo] update direction by info
                } else {
                    lk->setJointAxis(ainfo_->axis); // [todo] update direction by info
                }
                switch (ainfo_->getType()) {
                case ConnectingPoint::Rotational:
                    lk->setJointType(Link::RevoluteJoint);
                    break;
                case ConnectingPoint::Linear:
                    lk->setJointType(Link::PrismaticJoint);
                    break;
                case ConnectingPoint::Fixed: // ??
                    lk->setJointType(Link::RevoluteJoint);
                    lk->setJointRange(0, 0);
                    break;
                case ConnectingPoint::Free: // ??
                    lk->setJointType(Link::FreeJoint);
                    break;
                }
                DEBUG_STREAM(" limit : " << ainfo_->limit[0] << " / " << ainfo_->limit[1]);
                // [TODO] overwrite limit by info
                lk->setJointRange(ainfo_->limit[0], ainfo_->limit[1]);
                lk->setJointVelocityRange(ainfo_->vlimit[0], ainfo_->vlimit[1]);
                lk->setJointEffortRange(ainfo_->tqlimit[0], ainfo_->tqlimit[1]);
            } else {
                // no ainfo
            }
        } else { // not actuator(fixed connect)
            std::string nm_ = _pt->name();
            cinfo.getPartsName(_pt->name(), nm_);
            lk->setName(nm_);
            lk->setJointType(Link::FixedJoint);
            lk->setJointId(-1);
            map_link_cnoid_roboasm.insert(std::pair<std::string, std::string>(nm_, _pt->name()));
        }
    } // root or not root
    if (!!_pt->info && _pt->info->hasMassParam) {
        lk->setCenterOfMass(_pt->info->COM);
        lk->setMass(_pt->info->mass);
        lk->setInertia(_pt->info->inertia_tensor);
    }
#if 0
    // shape
    MeshGenerator mg;
    // com / next point
    { // red parts(origin)
        SgMeshPtr mesh = mg.generateBox(Vector3(0.004, 0.004, 0.004));
        SgShapePtr shape(new SgShape());
        shape->setMesh(mesh);
        SgMaterialPtr material(new SgMaterial());
        material->setDiffuseColor(Vector3f(1.0f, 0.0f, 0.0f));
        material->setEmissiveColor(Vector3f(0.0f, 0.0f, 0.0f));
        material->setAmbientIntensity(0.7f);
        shape->setMaterial(material);
        Position p; link_origin_to_self_.toPosition(p);
        SgPosTransformPtr trs(new SgPosTransform(p));
        trs->addChild(shape);
        lk->addShapeNode(trs); //
    }
    if(!_is_root)
    { // green self_cp
        SgMeshPtr mesh = mg.generateBox(Vector3(0.004, 0.004, 0.004));
        SgShapePtr shape(new SgShape());
        shape->setMesh(mesh);
        SgMaterialPtr material(new SgMaterial());
        material->setDiffuseColor(Vector3f(0.0f, 1.0f, 0.0f));
        material->setEmissiveColor(Vector3f(0.0f, 0.0f, 0.0f));
        material->setAmbientIntensity(0.7f);
        shape->setMaterial(material);
        // link_origin
        SgPosTransformPtr trs(new SgPosTransform());
        trs->addChild(shape);
        lk->addShapeNode(trs); //
    }
#endif
    if(!!_pt->info) {
        SgPosTransformPtr trs_vis;
        SgPosTransformPtr trs_col;
        // [TODO] update color by info
        Vector3f col_;
        if(_pt->info->visual.size() > 0) {
            trs_vis = (new SgPosTransform());
            trs_vis->setName(_pt->name() + "/visual");
            createSceneFromGeometry(trs_vis, _pt->info->visual, project_directory, _pt->color);
        }
        if(_pt->info->collision.size() > 0) {
            trs_col = (new SgPosTransform());
            trs_col->setName(_pt->name() + "/collision");
            createSceneFromGeometry(trs_col, _pt->info->collision, project_directory, _pt->color);
        }
        if(!!trs_vis) {
            Position p; link_origin_to_self_.toPosition(p);
            trs_vis->position() = p;
        }
        if(!!trs_col) {
            Position p; link_origin_to_self_.toPosition(p);
            trs_col->position() = p;
        }
        if (!!trs_vis) {
            lk->addVisualShapeNode(trs_vis);
        } else if (!!trs_col) {
            lk->addVisualShapeNode(trs_col);
        }
        if (!!trs_col) {
            lk->addCollisionShapeNode(trs_col);
        } else if (!!trs_vis) {
            lk->addCollisionShapeNode(trs_vis);
        }
    }
    return lk;
}
bool RoboasmBodyCreator::appendChildLink(BodyPtr _bd, Link *_lk, RoboasmPartsPtr _pt)
{
    partsPtrList plst;
    _pt->childParts(plst);
    for (auto it = plst.begin(); it != plst.end(); it++) {
        Link *clk = createLink(*it);
        if(!!clk) {
            _lk->appendChild(clk);
            _bd->updateLinkTree();
            appendChildLink(_bd, clk, (*it));
        }
    }
    return true;
}
BodyPtr RoboasmBodyCreator::_createBody(RoboasmRobotPtr _rb, const std::string &_name)
{
    _rb->updateDescendants();
    RoboasmPartsPtr root_ = _rb->rootParts();
    if(!root_) {
        ERROR_STREAM(" root not found");
        return nullptr;
    }

    body = new Body();
    if(_name.size() > 0) {
        body->setName(_name);
        body->setModelName(_rb->name());
    } else {
        body->setName(_rb->name());
        body->setModelName(_rb->name());
    }

    Link *lk = createLink(root_, true);
    body->setRootLink(lk);
    body->updateLinkTree();

    appendChildLink(body, lk, root_);

    return body;
}
BodyPtr RoboasmBodyCreator::createBody(RoboasmRobotPtr _rb, const std::string &_name)
{
    body = new Body();
    currentRobot = _rb;

    std::string local_name;
    if(_name.size() > 0) {
        local_name = _name;
    } else if (name.size() > 0) {
        local_name = name;
    } else if (!!info) {
        
    } else {
        local_name = _rb->name();
    }

    _createBody(_rb, local_name);

    if (merge_fixed_joint) {
        mergeFixedJoint(body);
    }

    return body;
}
static bool mergeLink(Link *plink, Link *clink)
{
    if(!plink) {
        std::cerr << "plink does not exist" << std::endl;
        return false;
    }
    if(!clink) {
        std::cerr << "clink does not exist" << std::endl;
        return false;
    }
    std::cerr << "p:" << plink->name() << " / " << "c:" << clink->name() << std::endl;
    LinkPtr link_protect(clink);
    std::vector<LinkPtr> all_child;
    {
        Link *cur = clink->child();
        while(!!cur) {
            all_child.push_back(cur);
            cur = cur->sibling();
        }
    }
    //std::cout << "child : " << all_child.size() << std::endl;
    // remove child
    if(!plink->removeChild(clink)) {
        std::cerr << "remove failed : " << clink->name() << std::endl;
        return false;
    }
    // update mass paramter of plink
    {
        double new_mass = plink->mass() + clink->mass();
        coordinates cds_Tb(clink->Tb());
        Vector3 p_c_c = clink->c();
        cds_Tb.transform_vector(p_c_c);
        Vector3 new_c = ((clink->mass() * p_c_c) + (plink->mass() * plink->c()))/new_mass;

        Matrix3 pIc = cds_Tb.rot * clink->I() * cds_Tb.rot.transpose();
        Matrix3 h_c = hat(new_c - p_c_c);
        Matrix3 h_p = hat(new_c - plink->c());

        Matrix3 newI = (pIc - clink->mass() * (h_c * h_c)) +
                       (plink->I() - plink->mass() * (h_p * h_p));
        plink->setInertia(newI);
        plink->setMass(new_mass);
        plink->setCenterOfMass(new_c);
    }
    const Position cTb = clink->Tb();
    {// update visual shape of plink
        SgGroup *vsp = clink->visualShape();
        if(!!vsp) {
            CloneMap cmp;
            vsp = new SgGroup(*vsp, &cmp);
        }
        SgPosTransform *posT = new SgPosTransform();
        posT->addChild(vsp);
        posT->setPosition(cTb);
        plink->addVisualShapeNode(posT);
    }
    {// update collision shape of plink
        SgGroup *csp = clink->collisionShape();
        if(!!csp) {
            CloneMap cmp;
            csp = new SgGroup(*csp, &cmp);
        }
        SgPosTransform *posT = new SgPosTransform();
        posT->addChild(csp);
        posT->setPosition(cTb);
        plink->addCollisionShapeNode(posT);
    }
    // append children of clink
    for(int i = 0; i < all_child.size(); i++) {
        // update offset of children of clink
        Position newTb = cTb * all_child[i]->Tb();
        all_child[i]->setOffsetPosition(newTb);
        // append children of clink to plink
        plink->appendChild(all_child[i]);
    }
    return true;
}
bool RoboasmBodyCreator::mergeFixedJoint(BodyPtr _bd)
{
    std::cerr << "merge" << std::endl;
    Link *lk = _bd->rootLink();
    bool do_loop = true;
    while(do_loop) {
        Link *clink = nullptr;
        Link *cur = lk->child();
        while(!!cur) {
            std::cerr << "nm: " << cur->name() << " : " << cur->isFixedJoint() << std::endl;
            if(cur->isFixedJoint()) {
                clink = cur;
                break;
            }
            if(!!cur->sibling()) {
                cur = cur->sibling();
            } else {
                if(!!cur->parent()->child()) {
                    cur = cur->parent()->child()->child();
                } else {
                    cur = nullptr;
                }
            }
        }
        if(!clink) {
            break;
        }
        std::cout << "clink : " << clink->name() << std::endl;
        if(!mergeLink(clink->parent(), clink)) {
            return false;
        }
        _bd->updateLinkTree();
    }
    return true;
}

} }
