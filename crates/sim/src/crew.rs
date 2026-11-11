//! How a crew shares its pot (design sections 2 and 10): equally by default,
//! or as the host sets the shares before launch, in whole percentages.

/// The most pilots in a crew (design section 2).
pub const MAX_CREW: usize = 4;

/// Equal whole percentages for `pilots`, summing to 100; the first pilots,
/// the host first, take the remainder: 34, 33, 33.
#[must_use]
pub fn equal_shares(pilots: usize) -> Vec<u32> {
    let Ok(count) = u32::try_from(pilots) else {
        return Vec::new();
    };
    if count == 0 {
        return Vec::new();
    }
    let (each, left) = (100 / count, 100 % count);
    (0..count).map(|i| each + u32::from(i < left)).collect()
}

/// Whether `shares` are a crew's split: one per pilot, whole percentages
/// summing to 100.
#[must_use]
pub fn valid_split(shares: &[u32], pilots: usize) -> bool {
    // Summed in u64: the shares come from a client, and a u32 sum can be
    // made to wrap round to 100.
    shares.len() == pilots && shares.iter().map(|&share| u64::from(share)).sum::<u64>() == 100
}

/// `amount` divided by `weights`, rounded down, with what rounding leaves
/// going to the first pilot with any weight, so the whole amount is paid. A
/// pilot weighted 0 (one who never launched) gets nothing.
#[must_use]
pub fn split(amount: u32, weights: &[u32]) -> Vec<u32> {
    let total: u64 = weights.iter().map(|&w| u64::from(w)).sum();
    if total == 0 {
        return vec![0; weights.len()];
    }
    let mut paid: Vec<u32> = weights
        .iter()
        .map(|&w| u32::try_from(u64::from(amount) * u64::from(w) / total).unwrap_or(0))
        .collect();
    let left = amount - paid.iter().sum::<u32>();
    if let Some(first) = weights.iter().position(|&w| w > 0) {
        paid[first] += left;
    }
    paid
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn equal_shares_sum_to_a_hundred_with_the_host_first() {
        assert_eq!(equal_shares(1), [100]);
        assert_eq!(equal_shares(2), [50, 50]);
        assert_eq!(equal_shares(3), [34, 33, 33]);
        assert_eq!(equal_shares(4), [25, 25, 25, 25]);
        assert!(equal_shares(0).is_empty());
    }

    #[test]
    fn a_split_is_one_share_each_summing_to_a_hundred() {
        assert!(valid_split(&[60, 40], 2));
        assert!(!valid_split(&[60, 30], 2));
        assert!(!valid_split(&[100], 2));
    }

    #[test]
    fn shares_that_only_wrap_round_to_a_hundred_are_not_a_split() {
        assert!(!valid_split(&[u32::MAX, 101], 2));
        assert!(!valid_split(&[u32::MAX, u32::MAX, 102], 3));
        assert!(!valid_split(&[u32::MAX; 4], 4));
    }

    #[test]
    fn splitting_pays_the_whole_amount_and_nothing_to_a_pilot_who_stayed_home() {
        assert_eq!(split(160, &[50, 50]), [80, 80]);
        assert_eq!(split(100, &[34, 33, 33]), [34, 33, 33]);
        assert_eq!(split(101, &[34, 33, 33]), [35, 33, 33]);
        // The third never launched: the others share by their weights.
        assert_eq!(split(220, &[34, 33, 0]), [112, 108, 0]);
        assert_eq!(split(50, &[0, 0]), [0, 0]);
        assert_eq!(split(7, &[0, 1, 1]), [0, 4, 3]);
    }
}
