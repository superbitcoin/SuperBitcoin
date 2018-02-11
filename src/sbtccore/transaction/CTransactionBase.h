///////////////////////////////////////////////////////////
//  CTransactionBase.h
//  Implementation of the Class CTransactionBase
//  Created on:     04-2-2018 15:55:57
//  Original author: ranger
///////////////////////////////////////////////////////////

#ifndef SUPERBITCOIN_CTRANSACTIONBASE_H
#define SUPERBITCOIN_CTRANSACTIONBASE_H


#include <utils/pubkey.h>

class  CValidationState;
enum CHECK_TYPE {
    ENTER_MEMPOOL,
    ACCEPT_TO_BLOCK
};

class CTransactionBase
{



public:
    virtual std::vector<CKeyID> const sender() const = 0;

    virtual std::vector<CKeyID> to() const = 0;

    virtual bool PreCheck(CHECK_TYPE type,CValidationState &state) const =0;

    virtual bool EndCheck(CHECK_TYPE type) const =0;

    virtual bool Excute(CHECK_TYPE type) const =0;

    virtual bool Undo(CHECK_TYPE type) const = 0;

    virtual std::string ToString() const = 0;


private:


protected:
    /// Type of transaction.
    enum Type
    {
        NullTransaction,                ///< Null transaction.
        Utox2UtoxTransaction,           ///< Utox2UtoxTransaction
        ContractCreation,               ///< Transaction to create contracts - receiveAddress() is ignored.
        MessageCall                     ///< Transaction to invoke a message call - receiveAddress() is used.
    };

    const Type type = NullTransaction;
public:
    CTransactionBase(Type type):type(type)
    {
    };

//private:
    CTransactionBase(){};

};


#endif //SUPERBITCOIN_CTRANSACTIONBASE_H
