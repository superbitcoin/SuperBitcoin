
set(   contractfile

#   bench/main.cpp
   eth/AccountManager.cpp
   eth/AccountManager.h
   eth/Farm.h
#   eth/main.cpp
   ethkey/KeyAux.h
#   ethkey/main.cpp
   ethminer/FarmClient.h
#   ethminer/main.cpp
   ethminer/MinerAux.h
#   ethvm/main.cpp
#   evmjit/examples/capi.c
#   evmjit/examples/examplevm.c
#   evmjit/include/evm.h
#   evmjit/include/evmjit.h
#   evmjit/libevmjit/preprocessor/llvm_includes_end.h
#   evmjit/libevmjit/preprocessor/llvm_includes_start.h
#   evmjit/libevmjit/Arith256.cpp
#   evmjit/libevmjit/Arith256.h
#   evmjit/libevmjit/Array.cpp
#   evmjit/libevmjit/Array.h
#   evmjit/libevmjit/BasicBlock.cpp
#   evmjit/libevmjit/BasicBlock.h
#   evmjit/libevmjit/Cache.cpp
#   evmjit/libevmjit/Cache.h
#   evmjit/libevmjit/Common.h
#   evmjit/libevmjit/Compiler.cpp
#   evmjit/libevmjit/Compiler.h
#   evmjit/libevmjit/CompilerHelper.cpp
#   evmjit/libevmjit/CompilerHelper.h
#   evmjit/libevmjit/Endianness.cpp
#   evmjit/libevmjit/Endianness.h
#   evmjit/libevmjit/ExecStats.cpp
#   evmjit/libevmjit/ExecStats.h
#   evmjit/libevmjit/Ext.cpp
#   evmjit/libevmjit/Ext.h
#   evmjit/libevmjit/GasMeter.cpp
#   evmjit/libevmjit/GasMeter.h
#   evmjit/libevmjit/Instruction.cpp
#   evmjit/libevmjit/Instruction.h
#   evmjit/libevmjit/JIT.cpp
#   evmjit/libevmjit/JIT.h
#   evmjit/libevmjit/Memory.cpp
#   evmjit/libevmjit/Memory.h
#   evmjit/libevmjit/Optimizer.cpp
#   evmjit/libevmjit/Optimizer.h
#   evmjit/libevmjit/RuntimeManager.cpp
#   evmjit/libevmjit/RuntimeManager.h
#   evmjit/libevmjit/Type.cpp
#   evmjit/libevmjit/Type.h
#   evmjit/libevmjit/Utils.cpp
#   evmjit/libevmjit/Utils.h
#   evmjit/tests/test-evm.cpp
#   evmjit/tests/test-evmjit-standalone.c
   libdevcore/Assertions.h
   libdevcore/Base64.cpp
   libdevcore/Base64.h
   libdevcore/Common.cpp
   libdevcore/Common.h
   libdevcore/CommonData.cpp
   libdevcore/CommonData.h
   libdevcore/CommonIO.cpp
   libdevcore/CommonIO.h
   libdevcore/CommonJS.cpp
   libdevcore/CommonJS.h
   libdevcore/concurrent_queue.h
   libdevcore/db.h
   libdevcore/debugbreak.h
   libdevcore/Exceptions.h
   libdevcore/FileSystem.cpp
   libdevcore/FileSystem.h
   libdevcore/FixedHash.cpp
   libdevcore/FixedHash.h
   libdevcore/Guards.cpp
   libdevcore/Guards.h
   libdevcore/Hash.cpp
   libdevcore/Hash.h
   libdevcore/Log.cpp
   libdevcore/Log.h
   libdevcore/MemoryDB.cpp
   libdevcore/MemoryDB.h
   libdevcore/OverlayDB.cpp
   libdevcore/OverlayDB.h
   libdevcore/picosha2.h
   libdevcore/RangeMask.h
   libdevcore/RLP.cpp
   libdevcore/RLP.h
   libdevcore/SHA3.cpp
   libdevcore/SHA3.h
   libdevcore/Terminal.h
   libdevcore/TransientDirectory.cpp
   libdevcore/TransientDirectory.h
   libdevcore/TrieCommon.cpp
   libdevcore/TrieCommon.h
   libdevcore/TrieDB.cpp
   libdevcore/TrieDB.h
   libdevcore/TrieHash.cpp
   libdevcore/TrieHash.h
   libdevcore/UndefMacros.h
   libdevcore/vector_ref.h
   libdevcore/Worker.cpp
   libdevcore/Worker.h
   libdevcrypto/AES.cpp
   libdevcrypto/AES.h
   libdevcrypto/Common.cpp
   libdevcrypto/Common.h
   libdevcrypto/CryptoPP.cpp
   libdevcrypto/CryptoPP.h
   libdevcrypto/ECDHE.cpp
   libdevcrypto/ECDHE.h
   libdevcrypto/Exceptions.h
   libdevcrypto/SecretStore.cpp
   libdevcrypto/SecretStore.h
   libethash/compiler.h
   libethash/data_sizes.h
   libethash/endian.h
   libethash/ethash.h
   libethash/fnv.h
   libethash/internal.c
   libethash/internal.h
   libethash/io.c
   libethash/io.h
   libethash/io_posix.c
#   libethash/io_win32.c
   libethash/mmap.h
#   libethash/mmap_win32.c
   libethash/sha3.c
   libethash/sha3.h
   libethash/util.c
   libethash/util.h
#   libethash/util_win32.c
   libethashseal/genesis/eip150Test.cpp
   libethashseal/genesis/eip158Test.cpp
   libethashseal/genesis/frontierTest.cpp
   libethashseal/genesis/homesteadTest.cpp
   libethashseal/genesis/mainNetwork.cpp
   libethashseal/genesis/mainNetworkTest.cpp
   libethashseal/genesis/metropolisTest.cpp
   libethashseal/genesis/sbtcMainNetwork.cpp
   libethashseal/genesis/sbtcTestNetwork.cpp
   libethashseal/genesis/ropsten.cpp
   libethashseal/genesis/transitionnetTest.cpp
   libethashseal/Ethash.cpp
   libethashseal/Ethash.h
   libethashseal/EthashAux.cpp
   libethashseal/EthashAux.h
   libethashseal/EthashCPUMiner.cpp
   libethashseal/EthashCPUMiner.h
   libethashseal/EthashProofOfWork.cpp
   libethashseal/EthashProofOfWork.h
   libethashseal/GenesisInfo.cpp
   libethashseal/GenesisInfo.h
   libethcore/ABI.cpp
   libethcore/ABI.h
   libethcore/BasicAuthority.cpp
   libethcore/BasicAuthority.h
   libethcore/BlockHeader.cpp
   libethcore/BlockHeader.h
   libethcore/ChainOperationParams.cpp
   libethcore/ChainOperationParams.h
   libethcore/Common.cpp
   libethcore/Common.h
   libethcore/CommonJS.cpp
   libethcore/CommonJS.h
   libethcore/Exceptions.h
   libethcore/ICAP.cpp
   libethcore/ICAP.h
   libethcore/KeyManager.cpp
   libethcore/KeyManager.h
   libethcore/Precompiled.cpp
   libethcore/Precompiled.h
   libethcore/SealEngine.cpp
   libethcore/SealEngine.h
   libethcore/Transaction.cpp
   libethcore/Transaction.h
   libethereum/Account.cpp
   libethereum/Account.h
   libethereum/BasicGasPricer.cpp
   libethereum/BasicGasPricer.h
   libethereum/Block.cpp
   libethereum/Block.h
   libethereum/BlockChain.cpp
   libethereum/BlockChain.h
   libethereum/BlockDetails.cpp
   libethereum/BlockDetails.h
   libethereum/BlockQueue.cpp
   libethereum/BlockQueue.h
   libethereum/ChainParams.cpp
   libethereum/ChainParams.h
   libethereum/CodeSizeCache.h
   libethereum/CommonNet.cpp
   libethereum/CommonNet.h
   libethereum/Defaults.cpp
   libethereum/Defaults.h
   libethereum/Executive.cpp
   libethereum/Executive.h
   libethereum/ExtVM.cpp
   libethereum/ExtVM.h
   libethereum/GasPricer.cpp
   libethereum/GasPricer.h
   libethereum/GenericFarm.h
   libethereum/GenericMiner.cpp
   libethereum/GenericMiner.h
   libethereum/GenesisInfo.cpp
   libethereum/GenesisInfo.h
   libethereum/Interface.cpp
   libethereum/Interface.h
   libethereum/LogFilter.cpp
   libethereum/LogFilter.h
   libethereum/State.cpp
   libethereum/State.h
   libethereum/Transaction.cpp
   libethereum/Transaction.h
   libethereum/TransactionQueue.cpp
   libethereum/TransactionQueue.h
   libethereum/TransactionReceipt.cpp
   libethereum/TransactionReceipt.h
   libethereum/VerifiedBlock.h
   libevm/All.h
   libevm/ExtVMFace.cpp
   libevm/ExtVMFace.h
#   libevm/JitVM.cpp
#   libevm/JitVM.h
#   libevm/SmartVM.cpp
#   libevm/SmartVM.h
   libevm/VM.cpp
   libevm/VM.h
   libevm/VMCalls.cpp
   libevm/VMConfig.h
   libevm/VMFace.h
   libevm/VMFactory.cpp
   libevm/VMFactory.h
   libevm/VMOpt.cpp
   libevm/VMValidate.cpp
   libevmcore/EVMSchedule.h
   libevmcore/Exceptions.h
   libevmcore/Instruction.cpp
   libevmcore/Instruction.h
   utils/json_spirit/json_spirit.h
   utils/json_spirit/json_spirit_error_position.h
   utils/json_spirit/json_spirit_reader.cpp
   utils/json_spirit/json_spirit_reader.h
   utils/json_spirit/json_spirit_reader_template.h
   utils/json_spirit/json_spirit_stream_reader.h
   utils/json_spirit/json_spirit_utils.h
   utils/json_spirit/json_spirit_value.cpp
   utils/json_spirit/json_spirit_value.h
   utils/json_spirit/json_spirit_writer.cpp
   utils/json_spirit/json_spirit_writer.h
   utils/json_spirit/json_spirit_writer_template.h
   utils/json_spirit/JsonSpiritHeaders.h
   utils/libscrypt/b64.c
   utils/libscrypt/b64.h
   utils/libscrypt/crypto-mcf.c
   utils/libscrypt/crypto-scrypt-saltgen.c
   utils/libscrypt/crypto_scrypt-check.c
   utils/libscrypt/crypto_scrypt-hash.c
   utils/libscrypt/crypto_scrypt-hexconvert.c
   utils/libscrypt/crypto_scrypt-hexconvert.h
   utils/libscrypt/crypto_scrypt-nosse.c
   utils/libscrypt/libscrypt.h
   utils/libscrypt/sha256.c
   utils/libscrypt/sha256.h
   utils/libscrypt/slowequals.c
   utils/libscrypt/slowequals.h
   utils/libscrypt/sysendian.h

)

add_library(contract ${contractfile} )