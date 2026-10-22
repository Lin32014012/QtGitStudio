#include "CredentialStoreFactory.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include "CredentialStoreWin.h"
#else
#include "CredentialStoreSession.h"
#endif

CredentialStore *createPlatformCredentialStore(QObject *parent) {
#ifdef Q_OS_WIN
    return new CredentialStoreWin(parent);
#else
    return new CredentialStoreSession(parent);
#endif
}
