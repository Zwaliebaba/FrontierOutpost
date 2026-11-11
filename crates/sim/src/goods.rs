//! What a home holds besides credits (design section 10): three raw materials
//! from the site and two goods processed at the outpost.

use serde::{Deserialize, Serialize};

use crate::site::Material;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Good {
    Ore,
    Salvage,
    Volatiles,
    Alloy,
    Components,
}

impl Good {
    pub const ALL: [Self; 5] = [
        Self::Ore,
        Self::Salvage,
        Self::Volatiles,
        Self::Alloy,
        Self::Components,
    ];

    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            Self::Ore => "ore",
            Self::Salvage => "salvage",
            Self::Volatiles => "volatiles",
            Self::Alloy => "alloy",
            Self::Components => "components",
        }
    }

    #[must_use]
    pub const fn index(self) -> usize {
        self as usize
    }

    /// The good a site material banks as.
    #[must_use]
    pub const fn from_material(material: Material) -> Self {
        match material {
            Material::Ore => Self::Ore,
            Material::Salvage => Self::Salvage,
            Material::Volatiles => Self::Volatiles,
        }
    }
}

/// A price in credits and goods.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Cost {
    pub credits: u32,
    pub goods: &'static [(Good, u32)],
}

impl Cost {
    pub const FREE: Self = Self {
        credits: 0,
        goods: &[],
    };

    #[must_use]
    pub const fn credits(credits: u32) -> Self {
        Self {
            credits,
            goods: &[],
        }
    }

    #[must_use]
    pub fn is_free(&self) -> bool {
        self.credits == 0 && self.goods.iter().all(|&(_, amount)| amount == 0)
    }
}
