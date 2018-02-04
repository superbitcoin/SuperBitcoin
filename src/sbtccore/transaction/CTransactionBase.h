///////////////////////////////////////////////////////////
//  CTransactionBase.h
//  Implementation of the Class CTransactionBase
//  Created on:     04-2-2018 15:55:57
//  Original author: ranger
///////////////////////////////////////////////////////////

#ifndef SUPERBITCOIN_CTRANSACTIONBASE_H
#define SUPERBITCOIN_CTRANSACTIONBASE_H


#include <utils/pubkey.h>

class CTransactionBase
{
public:
    CTransactionBase();

public:
    virtual std::vector<CKeyID> const sender() const = 0;

    virtual std::vector<CKeyID> to() const = 0;

    virtual bool PreCheck() const =0;

    virtual bool EndCheck() const =0;

    virtual bool Excute() const =0;

    virtual bool Undo() const = 0;

    virtual std::string ToString() const = 0;


private:


protected:
    /// Type of transaction.
    enum Type
    {
        NullTransaction,                ///< Null transaction.
        ContractCreation,                ///< Transaction to create contracts - receiveAddress() is ignored.
        MessageCall                        ///< Transaction to invoke a message call - receiveAddress() is used.
    };

    Type type = NullTransaction;


};


#endif //SUPERBITCOIN_CTRANSACTIONBASE_H
