#include "Interior.h"
#include "Queryable.h"
#include "Function.h"
#include "Iterator.h"
#include "StackFrame.h"
#include "Transform.h"

#include "Presentation.h"

ComponentInterior::~ComponentInterior() {
  if (Game::Presentation* presentation = Game::GetPresentation())
    presentation->ForgetInterior(this);
  for (size_t i = 0; i < objects.size(); ++i)
    objects[i]->Delete();
  objects.clear();
  objectMap.clear();
}

void ComponentInterior::Add(ObjectT* self, Object const& object) {
  if (object->container == self)
    return;

  if (object->container)
    object->container->RemoveInterior(object);
  object->container = self;

  objects.push(object);
  objectMap[object->GetType()].push(object);
  object->OnCreate();
}

void ComponentInterior::Draw(ObjectT* self, DrawState* state) {
  /* Drawn by the client's presentation, where there is one (ADR-016). */
  if (Game::Presentation* presentation = Game::GetPresentation())
    presentation->DrawInterior(self, state);
}

void ComponentInterior::Remove(ObjectT* self, Object const& object) {
  if (object->container == self) {
    object->container = nullptr;
    ComponentQueryable* qb = self->GetQueryable();
    if (qb)
      qb->Remove(object);
    objects.remove(object);
    objectMap[object->GetType()].remove(object);
    object->OnDestroy();
  }
}

void ComponentInterior::Run(ObjectT* self, UpdateState& state) {
  AUTO_FRAME;
  /* The interior's particles are the client's: it steps them, and makes them current for the
     effects the updates below set off (ADR-016). */
  Game::Presentation* presentation = Game::GetPresentation();
  if (presentation)
    presentation->BeginInteriorUpdate(this, state.dt);

  for (ObjectType type = 0; type < ObjectType_SIZE; ++type) {
    FRAME(ObjectType_String[type]) {
      if (!objectMap.contains(type))
        continue;

      Vector<Object>& objects = objectMap[type];
      for (int i = 0; i < (int)objects.size(); ++i) {
        Object object = objects[i];
        if (!object->IsDeleted())
          object->Update(state);
        if (object->IsDeleted()) {
          objects.removeIndex(i);
          i--;
          continue;
        }
      }
    }
  }

  /* Cleanup deleted objects from global list. */
  for (int i = 0; i < (int)objects.size(); ++i) {
    Object object = objects[i];
    if (object->IsDeleted()) {
      ComponentQueryable* qb = self->GetQueryable();
      if (qb)
        qb->Remove(object);
      objects.removeIndex(i);
      i--;
      continue;
    }
  }
  
  if (presentation)
    presentation->EndInteriorUpdate(this);
}

namespace {
  AutoClass(InteriorIterator,
    Object, object,
    size_t, index)
    InteriorIterator() {}
  };

  VoidFreeFunction(Object_AddInterior,
    "Add 'object' to the interior of 'interior'",
    Object, interior,
    Object, object)
  {
    interior->AddInterior(object);
  } FunctionAlias(Object_AddInterior, AddInterior);

  FreeFunction(InteriorIterator, Object_GetInteriorObjects,
    "Return an iterator to the objects inside 'container'",
    Object, container)
  {
    return InteriorIterator(container, 0);
  } FunctionAlias(Object_GetInteriorObjects, GetInteriorObjects);

  FreeFunction(Object, InteriorIterator_Access,
    "Return the contents of 'iterator'",
    InteriorIterator, iterator)
  {
    return iterator.object->GetInterior()->objects[iterator.index];
  } FunctionAlias(InteriorIterator_Access, Get);

  VoidFreeFunction(InteriorIterator_Advance,
    "Advance 'iterator'",
    InteriorIterator, iterator)
  {
    Mutable(iterator).index++;
  } FunctionAlias(InteriorIterator_Advance, Advance);

  FreeFunction(bool, InteriorIterator_HasMore,
    "Return whether 'iterator' has more elements",
    InteriorIterator, iterator)
  {
    return iterator.index < iterator.object->GetInterior()->objects.size();
  } FunctionAlias(InteriorIterator_HasMore, HasMore);
}
