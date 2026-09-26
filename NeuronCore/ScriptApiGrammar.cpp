#include "Function.h"
#include "Grammar.h"
#include "RNG.h"

FreeFunction(String, Grammar_Get,
  "Return the result of running the global grammar on 'text' using 'rng'",
  String, text,
  RNG, rng)
{
  return Grammar_Get()->Generate(rng, text, "");
}
