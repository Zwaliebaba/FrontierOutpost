//! A small seeded generator (mulberry32) for content generated from a seed.
//!
//! The same seed always gives the same sequence on every platform, which is
//! what design §10 needs: the same seed gives the same site.

#[derive(Debug, Clone)]
pub struct Rng(u32);

impl Rng {
    #[must_use]
    pub fn new(seed: u32) -> Self {
        Self(seed)
    }

    /// A number in 0..1.
    pub fn unit(&mut self) -> f32 {
        self.0 = self.0.wrapping_add(0x6d2b_79f5);
        let mut t = self.0;
        t = (t ^ (t >> 15)).wrapping_mul(t | 1);
        t ^= t.wrapping_add((t ^ (t >> 7)).wrapping_mul(t | 61));
        let bits = t ^ (t >> 14);
        // The top 24 bits are exact in an f32, so the result is never 1.0.
        (bits >> 8) as f32 / (1_u32 << 24) as f32
    }

    /// A number in min..max.
    pub fn between(&mut self, min: f32, max: f32) -> f32 {
        min + (max - min) * self.unit()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn repeats_from_its_seed_and_differs_between_seeds() {
        let draw = |seed| (0..5).map(|_| Rng::new(seed).unit()).collect::<Vec<_>>();
        let mut a = Rng::new(1);
        let mut b = Rng::new(1);
        let mut c = Rng::new(2);
        let (a, b, c): (Vec<f32>, Vec<f32>, Vec<f32>) = (
            (0..20).map(|_| a.unit()).collect(),
            (0..20).map(|_| b.unit()).collect(),
            (0..20).map(|_| c.unit()).collect(),
        );
        assert_eq!(a, b);
        assert_ne!(a, c);
        assert_eq!(draw(3), draw(3));
    }

    #[test]
    fn stays_within_its_range() {
        let mut rng = Rng::new(42);
        for _ in 0..10_000 {
            let value = rng.unit();
            assert!((0.0..1.0).contains(&value));
            let ranged = rng.between(-3.0, 5.0);
            assert!((-3.0..5.0).contains(&ranged));
        }
    }
}
