// Copyright (c) 2011-2020 The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_QT_OPENSYADDRESSVALIDATOR_H
#define OPENSY_QT_OPENSYADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class OpenSYAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit OpenSYAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

/** OpenSY address widget validator, checks for a valid opensy address.
 */
class OpenSYAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit OpenSYAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

#endif // OPENSY_QT_OPENSYADDRESSVALIDATOR_H
