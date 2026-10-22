#pragma once

class CredentialStore;
class QObject;

// Compile-time selection of the credential backend behind the
// CredentialStore interface:
//   Windows -> CredentialStoreWin (Windows Credential Manager)
//   others  -> CredentialStoreSession (in-memory, never written to disk)
CredentialStore *createPlatformCredentialStore(QObject *parent = nullptr);
