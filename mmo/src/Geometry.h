#ifndef _GEOMETRY_H_
#define _GEOMETRY_H_

#include "matrix.h"
#include <boost/shared_ptr.hpp>
#include <vector>

namespace geom {
   using namespace mat;
   using namespace std;

   struct Circle;
   struct Plane;

   //May want to know where they collided?
   struct GeometryBase {
      virtual bool collision(const GeometryBase *other) const =0;
      virtual bool collision(const Circle *other) const = 0;
      virtual bool collision(const Plane *other) const = 0;
      virtual GeometryBase *clone() const = 0;
   };

   struct Circle : public GeometryBase {
      Circle(vec2 pos, float radius) : pos(pos), radius(radius) {}
      bool collision(const GeometryBase *other) const;
      bool collision(const Circle *other) const;
      bool collision(const Plane *other) const;
      virtual GeometryBase *clone() const 
         { return new Circle(*this); }

      vec2 pos;
      float radius;
   };

   struct Plane : GeometryBase {
      Plane(vec2 v1, vec2 v2, float radius) : radius(radius) {
         vec2 temp = normalize(to(v1, v2));
         norm = vec2(-temp.y, -temp.x);
         d = -norm.dot(v1);
      }
      bool collision(const GeometryBase *other) const;
      bool collision(const Circle *other) const;
      bool collision(const Plane *other) const;
      virtual GeometryBase *clone() const
         { return new Plane(*this); }

      vec2 norm;
      float radius, d;
   };

   struct Geometry {
      Geometry(GeometryBase &g) : ptr(g.clone()) {}
      Geometry(GeometryBase *g) : ptr(g) {}
      bool collision(Geometry g) { return ptr->collision(g.ptr.get()); }
      boost::shared_ptr<GeometryBase> ptr;
   };
   
   struct GeometrySet : GeometryBase {
      GeometrySet() {}
      void add(Geometry g) { set.push_back(g); }
      bool collision(const GeometryBase *other) const;
      bool collision(const Circle *other) const;
      bool collision(const Plane *other) const;
      virtual GeometryBase *clone() const 
         { return new GeometrySet(*this); }
      vector<Geometry> set;
   };

}

#endif //_GEOMETRY_H_
