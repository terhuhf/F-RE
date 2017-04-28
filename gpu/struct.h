#pragma once

#include "mem.h"
#include "math.h"     // Math Library
typedef Vec3<float> Vertex;
typedef Vec3<float> Vector;
typedef Vec3<float> Point;
typedef Vec3<float> Normal;

struct Color
{
    union
    {
        struct
        {
            float r, g, b;
        };
        struct
        {
            float x, y, z;
        };
        Vec3<float> v;
    };
    __host__ __device__ Color() : x(.0f), y(.0f), z(.0f)
    {}
    __host__ __device__ Color(float _x, float _y, float _z) : x(_x), y(_y), z(_z)
    {}
    __host__ __device__ Color(const Vec3<float> &_v) : v(_v)
    {}
};

struct Ray
{
    Vertex pos, dir;
    Color factor;
};

struct Camera
{
    Vertex pos, dir;
    float fov_h, fov_v; // radius
};

// ---------------- Shape ----------------

typedef void Shape_t;

struct Sphere
{
    int strategy;
    Vertex center;
    float radius;
};

__device__ bool Intersect_ray2sphere(const void *ray, void *sphere, float *t)
{
    Ray &r = *(Ray *)ray;
    Sphere &s = *(Sphere *)sphere;

    Vertex op = s.center - r.pos;
    float eps = 1e-4;
    float b = op.dot(r.dir);
    float det = b * b - op.dot(op) + s.radius * s.radius;

    *t = 0.0f;
    if (det >= 0.0f)
    {
        det = sqrt(det);
        if (b - det > eps)
            *t = b - det;
        else if (b + det > eps)
            *t = b + det;
    }
    return *t != 0.0f;
}
__device__ void Normal_sphere(void *sphere, void *pos, void *normal)
{
    Sphere &s = *(Sphere *)sphere;
    Point &p = *(Point *)pos;
    Normal &nr = *(Normal *)normal;
    nr = (p - s.center);// .norm();
}

typedef bool(*intersect_t)(const void *, void *, float *);
__device__ intersect_t IntersectStrategy[1] = {Intersect_ray2sphere};

typedef void(*normal_t)(void *, void *, void *);
__device__ normal_t NormalStrategy[1] = {Normal_sphere};

struct HitParam
{
    float t;
    bool is_hit;
};
struct ComputeHit
{
    HitParam param;
    __device__ inline void compute(const Ray *ray, void *shape)
    {
        int strategy = *(int *)shape;
        param.is_hit = IntersectStrategy[strategy](ray, shape, &param.t);
    }
    __device__ inline bool isHit() const
    {
        return param.is_hit;
    }
    __device__ inline float t() const
    {
        return param.t;
    }
};

// ---------------- BSDF ----------------
#include "sampler.h"

struct BSDFParam
{
    Normal nr;
    Vector wo;
    float u1, u2;

    Vector wi;
    Color f;
    float pdf;
};

// BSDF models
struct Lambertian
{
    Color R;
};
//struct _FresnelConductor
//{
//	Color eta, k;
//};
//struct _FresnelDielectric
//{
//	float eta_i, eta_t;
//};
struct SpecularReflection
{
    Color R;
};
struct SpecularTransmission
{
    Color R;
};

// BSDF strategies
typedef void(*BSDF_t)(BSDFParam *, const void *);
__device__ void BSDF_Lambertian(BSDFParam *param, const void *_model)
{
    const Lambertian &model = *(const Lambertian *)_model;
    param->wi = UniformSampleHemisphere(param->u1, param->u2);
    if (param->wi.dot(param->nr) > 0.0f) param->wi = -param->wi;
    param->f.v = Vector::Scale(model.R.v, 1.0f / 3.14159f);
    param->pdf = 1.0f / 3.14159f;
}
__device__ void BSDF_SpecRefl(BSDFParam *param, const void *_model)
{
    const SpecularReflection &model = *(const SpecularReflection *)_model;
    Vector nr = param->nr;
    Vector wo = param->wo;
    nr.norm();
    param->wi = Vector(nr).scale(wo.dot(nr)).sub(wo).scale(2.0f).add(wo).norm();
    param->f.v = model.R.v;
    param->pdf = 1.0f;
}
__device__ void BSDF_SpecTrans(BSDFParam *param, const void *_model)
{
    const SpecularTransmission &model = *(const SpecularTransmission *)_model;
    Vector nr = param->nr;
    Vector wo = param->wo;
    nr.norm();
    param->wi = -Vector(nr).scale(wo.dot(nr)).sub(wo).scale(2.0f).add(wo).norm();
    param->f.v = model.R.v;
    param->pdf = 1.0f;
}
__device__ BSDF_t BSDFStrategy[] = {
    BSDF_Lambertian,
    BSDF_SpecRefl,
    BSDF_SpecTrans
};

// BSDF model management
// id => strategy: int
// id => model: void *
// GPU:
//      link memory:  [data_ptr, func_t] [data_ptr, func_t]
//      model memory: [model0] [model1] ...
enum bsdf_model_t
{
    LAMBERTIAN = 0, SPEC_REFL = 1, SPEC_TRANS = 2
};
struct _index_node // link model to model function
{
    void * mptr;
    bsdf_model_t mfunc;
};
union _model_node
{
    Lambertian diff;
    SpecularReflection refl;
    SpecularTransmission trans;
    _model_node()
    {}
};
typedef int bsdf_handle_t;

#include <cassert>
class BSDFFactory
{
    Pool<_index_node> inode_list;
    Pool<_model_node> mnode_list;
    size_t pos, size;
public:
    BSDFFactory(size_t _size)
        : pos(0), size(_size),
        inode_list(_size, IN_DEVICE | IN_HOST),
        mnode_list(_size, IN_DEVICE | IN_HOST)
    {}
    bsdf_handle_t createLambertian(Color R)
    {
        assert(pos < size);
        // mptr will be filled in syncToDevice()
        _index_node inode = {nullptr, LAMBERTIAN};
        Lambertian mnode = {R};
        inode_list.getHost()[pos] = inode;
        mnode_list.getHost()[pos].diff = mnode;
        return pos++;
    }
    bsdf_handle_t createSpecRefl(Color R)
    {
        assert(pos < size);
        // mptr will be filled in syncToDevice()
        _index_node inode = {nullptr, SPEC_REFL};
        SpecularReflection mnode = {R};
        inode_list.getHost()[pos] = inode;
        mnode_list.getHost()[pos].refl = mnode;
        return pos++;
    }
    bsdf_handle_t createSpecTrans(Color R)
    {
        assert(pos < size);
        // mptr will be filled in syncToDevice()
        _index_node inode = {nullptr, SPEC_TRANS};
        SpecularTransmission mnode = {R};
        inode_list.getHost()[pos] = inode;
        mnode_list.getHost()[pos].trans = mnode;
        return pos++;
    }
    void syncToDevice()
    {
        mnode_list.copyToDevice();
        _model_node *mptr = mnode_list.getDevice();
        for (size_t i = 0; i < inode_list.getSize(); ++i)
        {
            inode_list.getHost()[i].mptr = mptr + i;
        }
        inode_list.copyToDevice();
    }
    _index_node * getIndexNodeList()
    {
        return inode_list.getDevice();
    }
};

struct ComputeBSDF
{
    BSDFParam param;

    __device__ inline void compute(_index_node &inode)
    {
        BSDFStrategy[(int)inode.mfunc](&param, inode.mptr);
    }
    __device__ inline Color f() const
    {
        return param.f;
    }
    __device__ inline float pdf() const
    {
        return param.pdf;
    }
    __device__ inline Vector wi() const
    {
        return param.wi;
    }
};
struct BSDFPicker
{
    bsdf_handle_t model[3];
    float ratio[3];

    __device__ size_t pick(float r)
    {
        if (r <= ratio[0]) return model[0];
        if (r <= ratio[1]) return model[1];
        return model[2];
    }
};

// ---------------- Light ----------------

struct LightParam
{
    Color L;
};

struct PointLight
{
    Color intensity;
};

struct ComputeLight
{
    LightParam param;
    PointLight light;

    __device__ inline void compute(const Point &pos, const Vector &dir)
    {
        param.L = light.intensity; //Point::Scale(light.intensity.v, 1.f / DistanceSquared(light.pos, pos));
    }
    __device__ inline Color L() const
    {
        return param.L;
    }
};

// ---------------- Object ----------------

struct Object
{
    Shape_t *shape;
    BSDFPicker *bsdf;
    ComputeLight *light;
};
