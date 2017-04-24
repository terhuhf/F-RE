/* ====================================================
#   Author        : Terence (Yongxin) Feng
#   Email         : tyxfeng@gmail.com
#   File Name     : Object.cpp
#   Last Modified : 2017-03-21 20:18
# ====================================================*/

#include "Object.h"
#include "ShadeRec.h"
#include "sampler.h"
#include <cmath>
#include <cfloat>
#include <algorithm>

#define INV_PI 0.31831f

inline float
clamp(float x, float min, float max)
{
	return (x < min ? min : (x > max ? max : x));
}

/* NOTE: Implementation of Object */
Object::Object(void) {}

Object::~Object(void)
{	delete material_ptr; }

Point3D
Object::sample(void)
{	return Point3D();}
float
Object::pdf(ShadeRec&)
{	return 1; }
Normal
Object::get_normal(const Point3D&)
{	return Normal(); }

/* NOTE: Implementation of Sphere */
Sphere::Sphere(): center(), radius(0.0f) {}
Sphere::Sphere(const Point3D& ct, const float r, Material *m_):
	center(ct),
	radius(r)
{
	Object::set_material(m_);
}

bool
Sphere::hit(const Ray& ray, float& tmin, ShadeRec& sr)
{
	float t;
	Vector3D temp = ray.o - center;
	float a = ray.d * ray.d;
	float b = ray.d * 2.0f * temp;
	float c = temp * temp - radius * radius;
	float disc = b * b - 4.0f * a * c;

	if (disc < 0)
		return false;
	else {
		float e = sqrtf(disc);
		float denom = 2.0 * a;
		t = (-b - e) / denom;
		if (t > eps) {
			tmin = t;
			sr.normal = temp + ray.d * t;
			sr.local_hit_point = ray.o + ray.d * t;
			return true;
		}

		t = (-b + e) / denom;
		if (t > eps) {
			tmin = t;
			sr.normal = temp + ray.d * t;
			sr.local_hit_point = ray.o + ray.d * t;
			return true;
		}
	}
	return false;
}

bool
Sphere::shadow_hit(const Ray& ray, float& tmin)
{
	ShadeRec dummy_sr;
	return hit(ray, tmin, dummy_sr);
}

BBox
Sphere::get_bounding_box(void)
{
	float dist = sqrtf(3 * radius * radius);
	return BBox(center.x - dist, center.y - dist, center.z - dist,
		 center.x + dist, center.y + dist, center.z + dist);
}

void
Sphere::set_center(float x, float y, float z) { center = Point3D(x, y, z); }
void
Sphere::set_radius(float r) { radius = r; }


/* NOTE: Implementation of Plane */
Plane::Plane(): point(Point3D()), normal(Normal()) {}
Plane::Plane(const Point3D p, const Normal& n): point(p), normal(n) {}
Plane::Plane(const Point3D p, const Normal& n, const RGBColor c, float kd):
	point(p),
	normal(n)
{}

bool
Plane::hit(const Ray& ray, float& tmin, ShadeRec& sr)
{
	float t = (point - ray.o) * normal / (ray.d * normal);
	if (t > eps) {
		tmin = t;
		sr.normal = normal;
		sr.local_hit_point = ray.o + ray.d * t;
		return true;
	}
	return false;
}
bool
Plane::shadow_hit(const Ray& ray, float& tmin)
{
	ShadeRec sr;
	return hit(ray, tmin, sr);
}

BBox
Plane::get_bounding_box(void)
{
	/* not support BBox */
	return BBox();
}

/* NOTE: implementation of Rectangle */
Rectangle::Rectangle():
	Object()
{}
Rectangle::Rectangle(const Point3D& p0_, const Vector3D& a_, const Vector3D& b_):
	Object(),
	p0(p0_),
	a(a_),
	b(b_)
{
	inv_area = 1 / (a.length() * b.length());
	normal = a ^ b;
	normal.normalize();
	a_len = a.length();
	b_len = b.length();
	a_len_2 = a_len * a_len;
	b_len_2 = b_len * b_len;
}

bool
Rectangle::hit(const Ray& ray, float& tmin, ShadeRec& sr)
{
	float t = (p0 - ray.o) * normal / (ray.d * normal);
	if (t <= eps)
		return false;

	Point3D p = ray.o + ray.d * t;
	Vector3D d = p - p0;

	float ddota = d * a;
	if (ddota < 0.0 || ddota > a_len_2)
		return false;

	float ddotb = d * b;
	if (ddotb < 0.0 || ddotb > b_len_2)
		return false;

	tmin = t;
	sr.normal = normal;
	sr.local_hit_point = p;

	return true;
}

bool
Rectangle::shadow_hit(const Ray& ray, float& tmin)
{
	ShadeRec dummy_sr;
	return hit(ray, tmin, dummy_sr);
}

BBox
Rectangle::get_bounding_box(void)
{
	Point3D p1 = p0 + a;
	Point3D p2 = p0 + b;
	Point3D p3 = p1 + b;
	float x0, y0, z0;
	float x1, y1, z1;

	using namespace std;
	x0 = min(min(p0.x, p1.x), min(p2.x, p3.x));
	y0 = min(min(p0.y, p1.y), min(p2.y, p3.y));
	z0 = min(min(p0.z, p1.z), min(p2.z, p3.z));
	x1 = max(max(p0.x, p1.x), max(p2.x, p3.x));
	y1 = max(max(p0.y, p1.y), max(p2.x, p3.x));
	z1 = max(max(p0.z, p1.z), max(p2.z, p3.z));
	return BBox(x0, y0, z0, x1, y1, z1);
}

Point3D
Rectangle::sample(void)
{
	Point2D sample_point = sampler_ptr->sample_unit_square();
	return (p0 + a * sample_point.x + b * sample_point.y);
}

Normal
Rectangle::get_normal(const Point3D& p)
{
	return normal;
}

float
Rectangle::pdf(ShadeRec& sr)
{
	return inv_area;
}

/* NOTE: implementation of Triangle */
Triangle::Triangle():
	v0(0, 0, 0),
	v1(0, 0, 1),
	v2(1, 0, 0),
	normal(0, 1, 0)
{}

Triangle::Triangle(const Point3D& a, const Point3D& b, const Point3D& c):
	v0(a),
	v1(b),
	v2(c)
{
	normal = (v1 - v0) ^ (v2 - v0);
	normal.normalize();
}

bool
Triangle::hit(const Ray& ray, float& tmin, ShadeRec& sr)
{
	float a = v0.x - v1.x, b = v0.x - v2.x, c = ray.d.x, d = v0.x - ray.o.x;
	float e = v0.y - v1.y, f = v0.y - v2.y, g = ray.d.y, h = v0.y - ray.o.y;
	float i = v0.z - v1.z, j = v0.z - v2.z, k = ray.d.z, l = v0.z - ray.o.z;

	float m = f * k - g * j, n = h * k - g * l, p = f * l - h * j;
	float q = g * i - e * k, s = e * j - f * i;

	float inv_denom = 1.0 / (a * m + b * q + c * s);

	float e1 = d * m - b * n - c * p;
	float beta = e1 * inv_denom;

	if (beta < 0)
		return false;

	float r = e * l - h * i;
	float e2 = a * n + d * q + c * r;
	float gamma = e2 * inv_denom;

	if (gamma < 0)
		return false;

	if (beta + gamma > 1)
		return false;

	float e3 = a * p - b * r + d * s;
	float t = e3 * inv_denom;

	if (t < eps)
		return false;

	tmin = t;
	sr.normal = normal;
	sr.local_hit_point = ray.o + ray.d * t;
	return true;
}

bool
Triangle::shadow_hit(const Ray& ray, float& tmin)
{
	ShadeRec dummy_sr;
	return hit(ray, tmin, dummy_sr);
}

BBox
Triangle::get_bounding_box(void)
{
	float x0, y0, z0;
	float x1, y1, z1;
	using namespace std;
	x0 = min(v0.x, min(v1.x, v2.x));
	y0 = min(v0.y, min(v1.y, v2.y));
	z0 = min(v0.z, min(v1.z, v2.z));
	x1 = max(v0.x, max(v1.x, v2.x));
	y1 = max(v0.y, max(v1.y, v2.y));
	z1 = max(v0.z, max(v1.z, v2.z));
	return BBox(x0, y0, z0, x1, y1, z1);
}

/* NOTE: implementation of Compound */
Compound::Compound():
	object_ptrs()
{}

void
Compound::set_material(Material* m_ptr_)
{
	material_ptr = m_ptr_;
	for (Object* obj_ptr: object_ptrs)
		obj_ptr->set_material(m_ptr_);
}

void
Compound::add_object(Object* obj_ptr_)
{
	object_ptrs.push_back(obj_ptr_);
}

bool
Compound::hit(const Ray& ray, float& tmin, ShadeRec& sr)
{
	float t;
	bool hit = false;
	Normal normal;
	Point3D local_hit_point;
	tmin = FLT_MAX;

	for (Object* obj_ptr: object_ptrs)
	{
		if (obj_ptr->hit(ray, t, sr) && (t < tmin))
		{
			hit = true;
			tmin = t;
			normal = sr.normal;
			material_ptr = obj_ptr->material_ptr;
			local_hit_point = sr.local_hit_point;
		}
	}
	if (hit)
	{
		sr.t = tmin;
		sr.normal = normal;
		sr.local_hit_point = local_hit_point;
		return true;
	}
	return false;
}

bool
Compound::shadow_hit(const Ray& ray, float& tmin)
{
	ShadeRec dummy_sr;
	return hit(ray, tmin, dummy_sr);
}

BBox
Compound::get_bounding_box(void)
{
	BBox bbox;
	using namespace std;
	float x0 = FLT_MAX, y0 = FLT_MAX, z0 = FLT_MAX;
	float x1 = FLT_MIN, y1 = FLT_MIN, z1 = FLT_MIN;
	for (Object* obj_ptr: object_ptrs)
	{
		bbox = obj_ptr->get_bounding_box();
		if (bbox.x0 < x0) x0 = bbox.x0;
		if (bbox.y0 < y0) y0 = bbox.y0;
		if (bbox.z0 < z0) z0 = bbox.z0;
		if (bbox.x1 > x1) x1 = bbox.x1;
		if (bbox.y1 > y1) y1 = bbox.y1;
		if (bbox.z1 > z1) z1 = bbox.z1;
	}
	return BBox(x0, y0, z0, x1, y1, z1);
}
