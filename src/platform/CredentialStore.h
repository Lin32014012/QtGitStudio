// v4-P2-2 CredentialStore - abstract credential persistence interface.
// Deliberately NOT bound to a concrete backend: the Windows Credential
// Manager implementation can be plugged in later without touching
// GitHubAccountService or the UI. Never store passwords/tokens in plain
// ini/json files.
#ifndef CREDENTIALSTORE_H
#define CREDENTIALSTORE_H

#include <QObject>
#include <QString>

class CredentialStore : public QObject {
public:
    explicit CredentialStore(QObject *parent = nullptr) : QObject(parent) {}

    // Persist (username, token). True on success.
    virtual bool save(const QString &username, const QString &token) = 0;
    // Read stored credentials. Returns false when nothing is stored or
    // reading failed (caller must treat this as logged out, never crash).
    virtual bool read(QString *username, QString *token) = 0;
    // Remove stored credentials. True on success (also when absent).
    virtual bool remove() = 0;
};

#endif // CREDENTIALSTORE_H
