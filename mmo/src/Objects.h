#ifndef _SERVER_OM_H_
#define _SERVER_OM_H_

#include "Regions.h"
#include "Geometry.h"
#include "matrix.h"
#include "packet.h"
#include "Util.h"
#include "Constants.h"
#include <vector>
#include <map>

namespace objectManager {
   //using namespace std;
   using namespace mat;
   using namespace constants;
   using namespace geom;
   using namespace regionManager;

   struct ObjectBase : RMObject {
      ObjectBase(int id, vec2 pos)
         : RMObject(id), pos(pos) {}
      virtual int getType() const = 0;
      virtual float getRadius() const = 0;
      Geometry getGeom() const {
         return Circle(pos, getRadius());
      }
      vec2 pos;
   };

   struct PlayerBase : ObjectBase {
      PlayerBase(int id, vec2 pos) 
         : ObjectBase(id, pos) {}
      int getType() const;
      float getRadius() const;
   };

   struct NPCBase : ObjectBase {
      NPCBase(int id, int type, vec2 pos) 
         : ObjectBase(id, pos), type(type) {}
      int getType() const;
      float getRadius() const;
      int type;
   };

   struct ItemBase : ObjectBase {
      ItemBase(int id, int type, vec2 pos) 
         : ObjectBase(id, pos), type(type) {}
      int getType() const;
      float getRadius() const;
      int type;
   };

   struct MissileBase : ObjectBase {
      MissileBase(int id, int type, vec2 pos) 
         : ObjectBase(id, pos), type(type) {}
      int getType() const;
      float getRadius() const;
      int type;
   };

   struct ObjectManager {
      ObjectManager();

      bool inBounds(vec2 pos) const;
      PlayerBase *getPlayer(int id);
      MissileBase *getMissile(int id);
      NPCBase *getNpc(int id);
      ItemBase *getItem(int id);
      
      bool add(PlayerBase *p);
      bool add(MissileBase *m);
      bool add(NPCBase *n);
      bool add(ItemBase *i);
      
      bool remove(int id);
      bool check(int id, int type);
      
      void collidingPlayers(Geometry g, vec2 center, 
         std::vector<PlayerBase *> &collided);
      void collidingMissiles(Geometry g, vec2 center,
         std::vector<MissileBase *> &collided);
      void collidingNPCs(Geometry g, vec2 center,
         std::vector<NPCBase *> &collided);
      void collidingItems(Geometry g, vec2 center,
         std::vector<ItemBase *> &collided);
      
      void move(PlayerBase *p, vec2 newPos);
      void move(ItemBase *i, vec2 newPos);
      void move(MissileBase *m, vec2 newPos);
      void move(NPCBase *n, vec2 newPos);
      
      ObjectBase *get(int type, int index_Not_The_Id);
      
      unsigned itemCount() const;
      unsigned playerCount() const;
      unsigned npcCount() const;
      unsigned missileCount() const;
      
      vec2 worldBotLeft;
      
   protected:
      void _move(ObjectBase *obj, vec2 &pos, vec2 &newPos);
      ObjectBase *_get(int id, int type);
      ObjectBase *_get(int id);
      bool _add(ObjectBase *obj, vec2 pos, Geometry g);
      template<typename Ty, int ObjectTy>
      vector<Ty> &_colliding(Geometry g, const vec2 &center, 
         std::vector<Ty> &collided);
      vec2 toWorldPos(vec2 pos);
      void getRegion(vec2 pos, int &x, int &y);
      void getRegions(vec2 pos, Geometry g, std::vector<int> &regionIds);
      Geometry getRegionGeom(int x, int y);
      
      RegionManager rm;
   };

   template<typename Ty, int ObjectTy>
   vector<Ty> &ObjectManager::_colliding(Geometry g, const vec2 &center, 
      std::vector<Ty> &collided)
   {
      std::vector<int> regionIds;
      getRegions(center, g, regionIds);
      int counted = 0;
      for(unsigned i = 0; i < regionIds.size(); i++) {
         Region &region = *rm.getRegion(regionIds[i]);
         std::vector<RMObject *> &objs
            = region.getObjects(ObjectTy);
         for(unsigned j = 0; j < objs.size(); j++) {
            Ty obj = static_cast<Ty>(objs[j]);
            if(obj->getGeom().collision(g)) {
               collided.push_back(obj);
            }
         }
      }
      util::removeDuplicates(collided);
      return collided;
   }

} // end server namespace
  

#endif