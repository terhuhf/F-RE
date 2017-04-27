#pragma once

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

#include <vector>
#include <deque>
class BSDFModelPool
{
    // (model type, model pointer)
    std::vector<int> mtype;
    std::deque<void *> mpool;
public:
    int CreateLambertian(Color R)
    {
        int id = (int)mtype.size();
        mtype.push_back(0);
        Pool<Lambertian> *p = new Pool<Lambertian>(1, IN_HOST | IN_DEVICE);
        p->getHost()->R = R;
        mpool.push_back(p);
        return id;
    }
    int CreateSpecRefl(Color R)
    {
        int id = (int)mtype.size();
        mtype.push_back(1);
        Pool<SpecularReflection> *p = new Pool<SpecularReflection>(1, IN_HOST | IN_DEVICE);
        p->getHost()->R = R;
        mpool.push_back(p);
        return id;
    }
};

// BSDF strategies
__device__ void BSDF_Lambertian(BSDFParam *param, const void *_model, float u1, float u2)
{
    const Lambertian &model = *(const Lambertian *)_model;
    param->wi = UniformSampleHemisphere(u1, u2);
    if (param->wi.dot(param->nr) > 0.0f) param->wi = -param->wi;
    param->f.v = Vector::Scale(model.R.v, 1.0f / 3.14159f);
    param->pdf = 1.0f / 3.14159f;
}
__device__ void BSDF_SpecRefl(BSDFParam *param, const void *_model, float u1, float u2)
{
    const SpecularReflection &model = *(const SpecularReflection *)_model;
    Vector nr = param->nr;
    Vector wo = param->wo;
    nr.norm();
    param->wi = Vector(nr).scale(wo.dot(nr)).sub(wo).scale(2.0f).add(wo).norm();
    param->f.v = model.R.v;
    param->pdf = 1.0f;
}
__device__ void BSDF_SpecTrans(BSDFParam *param, const void *_model, float u1, float u2)
{
    const SpecularTransmission &model = *(const SpecularTransmission *)_model;
    Vector nr = param->nr;
    Vector wo = param->wo;
    nr.norm();
    param->wi = -Vector(nr).scale(wo.dot(nr)).sub(wo).scale(2.0f).add(wo).norm();
    param->f.v = model.R.v;
    param->pdf = 1.0f;
}

typedef void(*BSDF_t)(BSDFParam *, const void *, float, float);
__device__ BSDF_t BSDFStrategy[] = {
    BSDF_Lambertian,
    BSDF_SpecRefl,
    BSDF_SpecTrans
};

struct ComputeBSDF
{
    BSDFParam param;

    // model number, type_id, func, %
    //model_t model[3];
    int model_id, model_func;
    float ratio;

    __device__ inline void compute(float pick, Normal nr, Vector wo, float u1, float u2)
    {
        param.nr = nr;
        param.wo = wo;
        BSDFStrategy[model_func](&param, &model_id, u1, u2);
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
    ComputeBSDF *bsdf;
    ComputeLight *light;
};

