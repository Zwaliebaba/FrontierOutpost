// Trigonometry.cpp -- CORDIC, in integers, because the simulation has to give the same answer
// twice (R16).
//
// CORDIC rotates a vector by a sum of fixed angles whose tangents are powers of two, so every
// step is a shift and an add. There is no multiply, no table of sines, no floating point and
// nothing whose result depends on the order the compiler chose to evaluate something in. The same
// two functions come out of one primitive: driving the angle to zero rotates (rotation mode,
// giving sine and cosine), driving the y component to zero measures it (vectoring mode, giving
// the arc tangent).
//
// The tables below were generated and their error measured before being pasted here; the figures
// quoted in Trigonometry.h are from that measurement, not from an estimate.

#include "pch.h"
#include "Trigonometry.h"

namespace Neuron
{

namespace
{

// atan(2^-i), in turns16. Fifteen entries is where the table reaches the resolution of a Turns16:
// the last two are already 1 unit, and a sixteenth would be 0 and do nothing.
constexpr std::size_t CORDIC_ITERATIONS = 15;
constexpr std::array<std::int32_t, CORDIC_ITERATIONS> ATAN_TURNS16 = {
  8192, 4836, 2555, 1297, 651, 326, 163, 81, 41, 20, 10, 5, 3, 1, 1,
};

// Each CORDIC step lengthens the vector by sqrt(1 + 2^-2i), and the product of those over the
// fifteen steps is 1/0.6072529354. Starting the rotation at that reciprocal instead of at 1.0 is
// what makes the answer come out already scaled: 65536 * 0.6072529354 = 39797.
constexpr std::int32_t CORDIC_SEED = 39797;

// Beyond this, the shifts inside the loop would overflow once CORDIC's own gain is applied. The
// inputs are scaled down until they fit, which costs precision the answer does not have anyway:
// an angle is 16 bits and these are 30.
constexpr std::int64_t CORDIC_INPUT_LIMIT = 1LL << 30;

// And below this they are scaled UP, which matters more than it looks. Vectoring shifts its
// running vector right by the iteration number, so an input of a few hundred has shifted to zero
// by iteration ten -- and every iteration after that subtracts its whole angle with nothing left
// to correct against. Left unfixed, atan2 of a small vector is out by twenty units or so, which
// is how a ship told to fly along +X flies a fraction of a degree off it. Scaling both components
// by the same power of two does not change the direction at all.
constexpr std::int64_t CORDIC_UPSCALE_TARGET = 1LL << 28;

/// Sine and cosine on the axes, exact. Index by quarter turn.
constexpr std::array<SineCosine, 4> CARDINAL_DIRECTIONS = {
  SineCosine{0, TRIG_ONE},
  SineCosine{TRIG_ONE, 0},
  SineCosine{0, -TRIG_ONE},
  SineCosine{-TRIG_ONE, 0},
};

} // namespace

SineCosine SineCosineTurns16(Turns16 _angle) noexcept
{
  // The four cardinal directions are exact by construction rather than by CORDIC coming close to
  // them. Heading zero has to be exactly +X: the rotation leaves a residual angle of up to one
  // unit, which is a sine of about six parts in 65536, and a ship told to fly along an axis
  // should fly along it rather than a thousandth of a degree off. It also makes the axes
  // something the tests can assert rather than approximate.
  if ((_angle & (TURNS16_PER_QUARTER_TURN - 1)) == 0)
  {
    return CARDINAL_DIRECTIONS[(_angle >> 14) & 3];
  }

  // CORDIC converges over roughly +/- 99 degrees, so the angle is folded into the first quadrant
  // and the whole quarter turns are put back afterwards as exact 90 degree rotations -- which are
  // a swap and a negation, and introduce no error at all.
  const std::int32_t quadrant = (_angle >> 14) & 3;
  std::int32_t x = CORDIC_SEED;
  std::int32_t y = 0;
  std::int32_t z = _angle & (TURNS16_PER_QUARTER_TURN - 1);

  for (std::size_t iteration = 0; iteration < CORDIC_ITERATIONS; ++iteration)
  {
    const std::int32_t shiftedX = x >> iteration;
    const std::int32_t shiftedY = y >> iteration;

    if (z >= 0)
    {
      x -= shiftedY;
      y += shiftedX;
      z -= ATAN_TURNS16[iteration];
    }
    else
    {
      x += shiftedY;
      y -= shiftedX;
      z += ATAN_TURNS16[iteration];
    }
  }

  for (std::int32_t turned = 0; turned < quadrant; ++turned)
  {
    const std::int32_t rotatedX = -y;
    y = x;
    x = rotatedX;
  }

  return SineCosine{y, x};
}

Turns16 Atan2Turns16(std::int64_t _x, std::int64_t _z) noexcept
{
  if (_x == 0 && _z == 0)
  {
    return 0;
  }

  // Fold into the first quadrant by whole quarter turns, then put them back at the end.
  std::int32_t quadrant = 0;
  std::int64_t x = _x;
  std::int64_t z = _z;
  if (x < 0 && z >= 0)
  {
    const std::int64_t rotatedX = z;
    z = -x;
    x = rotatedX;
    quadrant = 1;
  }
  else if (x < 0)
  {
    x = -x;
    z = -z;
    quadrant = 2;
  }
  else if (z < 0)
  {
    const std::int64_t rotatedX = -z;
    z = x;
    x = rotatedX;
    quadrant = 3;
  }

  while (x > CORDIC_INPUT_LIMIT || z > CORDIC_INPUT_LIMIT || x < -CORDIC_INPUT_LIMIT || z < -CORDIC_INPUT_LIMIT)
  {
    x >>= 1;
    z >>= 1;
  }

  while (x < CORDIC_UPSCALE_TARGET && z < CORDIC_UPSCALE_TARGET && (x != 0 || z != 0))
  {
    x <<= 1;
    z <<= 1;
  }

  std::int32_t angle = 0;
  for (std::size_t iteration = 0; iteration < CORDIC_ITERATIONS; ++iteration)
  {
    const std::int64_t shiftedX = x >> iteration;
    const std::int64_t shiftedZ = z >> iteration;

    if (z > 0)
    {
      x += shiftedZ;
      z -= shiftedX;
      angle += ATAN_TURNS16[iteration];
    }
    else
    {
      x -= shiftedZ;
      z += shiftedX;
      angle -= ATAN_TURNS16[iteration];
    }
  }

  const auto wrapped = static_cast<std::uint32_t>(angle + quadrant * TURNS16_PER_QUARTER_TURN);
  return static_cast<Turns16>(wrapped & 0xFFFFU);
}

std::uint64_t IntegerSquareRoot(std::uint64_t _value) noexcept
{
  if (_value == 0)
  {
    return 0;
  }

  // Digit-by-digit in base four: one bit of the answer per step, using only shifts, adds and
  // compares. Sixty-four bits in, at most thirty-two steps, and exact.
  std::uint64_t remainder = _value;
  std::uint64_t result = 0;
  std::uint64_t bit = 1ULL << 62;

  while (bit > remainder)
  {
    bit >>= 2;
  }

  while (bit != 0)
  {
    if (remainder >= result + bit)
    {
      remainder -= result + bit;
      result = (result >> 1) + bit;
    }
    else
    {
      result >>= 1;
    }
    bit >>= 2;
  }

  return result;
}

} // namespace Neuron
