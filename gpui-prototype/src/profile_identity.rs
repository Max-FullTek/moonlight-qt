use serde::{Deserialize, Serialize};
use std::{fmt, io::Write, path::Path};

#[derive(Deserialize, Serialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct ProfileIdentity {
    version: u32,
    unique_id: String,
    certificate: String,
    private_key: String,
}

impl fmt::Debug for ProfileIdentity {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ProfileIdentity").finish_non_exhaustive()
    }
}

impl ProfileIdentity {
    pub fn install(&self, profile: &Path) -> anyhow::Result<()> {
        anyhow::ensure!(
            self.version == 1 && !self.unique_id.is_empty() &&
            !self.certificate.is_empty() && !self.private_key.is_empty(),
            "核心傳回的配對身分無效"
        );
        let path = profile.join("identity-v1.json");
        let mut pending = tempfile::NamedTempFile::new_in(profile)?;
        serde_json::to_writer(pending.as_file_mut(), self)?;
        pending.flush()?;
        pending.as_file().sync_all()?;
        match pending.persist_noclobber(&path) {
            Ok(_) => Ok(()),
            Err(error) if error.error.kind() == std::io::ErrorKind::AlreadyExists => {
                let installed: Self = serde_json::from_slice(&std::fs::read(&path)?)?;
                anyhow::ensure!(installed == *self, "配對身分已由另一個核心建立");
                Ok(())
            }
            Err(error) => Err(error.error.into()),
        }
    }
}
