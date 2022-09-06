
#include "src/primihub/operator/aby3_operator.h"

namespace primihub {
int MPCOperator::setup(std::string ip, u32 next_port, u32 prev_port) {
  CommPkg comm = CommPkg();
  Session ep_next_;
  Session ep_prev_;

  switch (partyIdx) {
  case 0:
    ep_next_.start(ios, ip, next_port, SessionMode::Server, next_name);
    ep_prev_.start(ios, ip, prev_port, SessionMode::Server, prev_name);

    VLOG(3) << "Start server session, listen port " << next_port << ".";
    VLOG(3) << "Start server session, listen port " << prev_port << ".";

    break;
  case 1:
    ep_next_.start(ios, ip, next_port, SessionMode::Server, next_name);
    ep_prev_.start(ios, ip, prev_port, SessionMode::Client, prev_name);

    VLOG(3) << "Start server session, listen port " << next_port << ".";
    VLOG(3) << "Start client session, connect to " << ip << ":" << prev_port
            << ".";

    break;
  default:
    ep_next_.start(ios, ip, next_port, SessionMode::Client, next_name);
    ep_prev_.start(ios, ip, prev_port, SessionMode::Client, prev_name);

    VLOG(3) << "Start client session, connect to " << ip << ":" << next_port
            << ".";
    VLOG(3) << "Start client session, connect to " << ip << ":" << prev_port
            << ".";

    break;
  }

  comm.setNext(ep_next_.addChannel());
  comm.setPrev(ep_prev_.addChannel());
  comm.mNext().waitForConnection();
  comm.mPrev().waitForConnection();
  comm.mNext().send(partyIdx);
  comm.mPrev().send(partyIdx);

  u64 prev_party = 0;
  u64 next_party = 0;
  comm.mNext().recv(next_party);
  comm.mPrev().recv(prev_party);
  if (next_party != (partyIdx + 1) % 3) {
    LOG(ERROR) << "Party " << partyIdx << ", expect next party id "
               << (partyIdx + 1) % 3 << ", but give " << next_party << ".";
    return -3;
  }

  if (prev_party != (partyIdx + 2) % 3) {
    LOG(ERROR) << "Party " << partyIdx << ", expect prev party id "
               << (partyIdx + 2) % 3 << ", but give " << prev_party << ".";
    return -3;
  }

  // Establishes some shared randomness needed for the later protocols
  enc.init(partyIdx, comm, sysRandomSeed());

  // Establishes some shared randomness needed for the later protocols
  eval.init(partyIdx, comm, sysRandomSeed());

  binEval.mPrng.SetSeed(toBlock(partyIdx));
  gen.init(toBlock(partyIdx), toBlock((partyIdx + 1) % 3));

  // Copies the Channels and will use them for later protcols.
  mNext = comm.mNext();
  mPrev = comm.mPrev();
  auto commPtr = std::make_shared<CommPkg>(comm.mPrev(), comm.mNext());
  runtime.init(partyIdx, commPtr);
  return 1;
}

void MPCOperator::fini() {
  mPrev.close();
  mNext.close();
}
void MPCOperator::createShares(const i64Matrix &vals,
                               si64Matrix &sharedMatrix) {
  enc.localIntMatrix(runtime, vals, sharedMatrix).get();
}

void MPCOperator::createShares(si64Matrix &sharedMatrix) {
  enc.remoteIntMatrix(runtime, sharedMatrix).get();
}
si64Matrix MPCOperator::createSharesByShape(const i64Matrix &val) {
  std::array<u64, 2> size{val.rows(), val.cols()};
  mNext.asyncSendCopy(size);
  mPrev.asyncSendCopy(size);
  si64Matrix dest(size[0], size[1]);
  enc.localIntMatrix(runtime, val, dest).get();
  return dest;
}

si64Matrix MPCOperator::createSharesByShape(u64 pIdx) {
  std::array<u64, 2> size;
  if (pIdx == (partyIdx + 1) % 3)
    mNext.recv(size);
  else if (pIdx == (partyIdx + 2) % 3)
    mPrev.recv(size);
  else
    throw RTE_LOC;

  si64Matrix dest(size[0], size[1]);
  enc.remoteIntMatrix(runtime, dest).get();
  return dest;
}

// only support val is column vector
sbMatrix MPCOperator::createBinSharesByShape(i64Matrix &val, u64 bitCount) {
  std::array<u64, 2> size{val.rows(), bitCount};
  mNext.asyncSendCopy(size);
  mPrev.asyncSendCopy(size);
  sbMatrix dest(size[0], size[1]);
  enc.localBinMatrix(runtime, val, dest).get();
  return dest;
}

sbMatrix MPCOperator::createBinSharesByShape(u64 pIdx) {
  std::array<u64, 2> size;
  if (pIdx == (partyIdx + 1) % 3)
    mNext.recv(size);
  else if (pIdx == (partyIdx + 2) % 3)
    mPrev.recv(size);
  else
    throw RTE_LOC;

  sbMatrix dest(size[0], size[1]);
  enc.remoteBinMatrix(runtime, dest).get();
  return dest;
}

i64Matrix MPCOperator::revealAll(const si64Matrix &vals) {
  i64Matrix ret(vals.rows(), vals.cols());
  enc.revealAll(runtime, vals, ret).get();
  return ret;
}

i64Matrix MPCOperator::reveal(const si64Matrix &vals) {
  i64Matrix ret(vals.rows(), vals.cols());
  enc.reveal(runtime, vals, ret).get();
  return ret;
}

void MPCOperator::reveal(const si64Matrix &vals, u64 Idx) {
  enc.reveal(runtime, Idx, vals).get();
}

si64Matrix MPCOperator::MPC_Add(std::vector<si64Matrix> sharedInt) {
  si64Matrix sum;
  sum = sharedInt[0];
  for (u64 i = 1; i < sharedInt.size(); i++) {
    sum = sum + sharedInt[i];
  }
  return sum;
}

si64 MPCOperator::MPC_Add_Const(i64 constInt, si64 &sharedInt) {
  si64 temp = sharedInt;
  if (partyIdx == 0)
    temp[0] = sharedInt[0] + constInt;
  else if (partyIdx == 1)
    temp[1] = sharedInt[1] + constInt;
  return temp;
}

si64Matrix MPCOperator::MPC_Add_Const(i64 constInt,
                                      si64Matrix &sharedIntMatrix) {
  si64Matrix temp = sharedIntMatrix;
  if (partyIdx == 0)
    for (i64 i = 0; i < sharedIntMatrix.rows(); i++)
      for (i64 j = 0; j < sharedIntMatrix.cols(); j++)
        temp[0](i, j) = sharedIntMatrix[0](i, j) + constInt;
  else if (partyIdx == 1)
    for (i64 i = 0; i < sharedIntMatrix.rows(); i++)
      for (i64 j = 0; j < sharedIntMatrix.cols(); j++)
        temp[1](i, j) = sharedIntMatrix[1](i, j) + constInt;
  return temp;
}

si64Matrix MPCOperator::MPC_Sub(si64Matrix minuend,
                                std::vector<si64Matrix> subtrahends) {
  si64Matrix difference;
  difference = minuend;
  for (u64 i = 0; i < subtrahends.size(); i++) {
    difference = difference - subtrahends[i];
  }
  return difference;
}

si64 MPCOperator::MPC_Sub_Const(i64 constInt, si64 &sharedInt, bool mode) {
  si64 temp = sharedInt;
  if (partyIdx == 0)
    temp[0] = sharedInt[0] - constInt;
  else if (partyIdx == 1)
    temp[1] = sharedInt[1] - constInt;
  if (mode != true) {
    temp[0] = -temp[0];
    temp[1] = -temp[1];
  }
  return temp;
}

si64Matrix MPCOperator::MPC_Sub_Const(i64 constInt, si64Matrix &sharedIntMatrix,
                                      bool mode) {
  si64Matrix temp = sharedIntMatrix;
  if (partyIdx == 0)
    for (i64 i = 0; i < sharedIntMatrix.rows(); i++)
      for (i64 j = 0; j < sharedIntMatrix.cols(); j++)
        temp[0](i, j) = sharedIntMatrix[0](i, j) - constInt;
  else if (partyIdx == 1)
    for (i64 i = 0; i < sharedIntMatrix.rows(); i++)
      for (i64 j = 0; j < sharedIntMatrix.cols(); j++)
        temp[1](i, j) = sharedIntMatrix[1](i, j) - constInt;
  if (mode != true) {
    temp[0] = -temp[0];
    temp[1] = -temp[1];
  }
  return temp;
}

si64Matrix MPCOperator::MPC_Mul(std::vector<si64Matrix> sharedInt) {
  si64Matrix prod;
  prod = sharedInt[0];
  for (u64 i = 1; i < sharedInt.size(); ++i)
    eval.asyncMul(runtime, prod, sharedInt[i], prod).get();
  return prod;
}

si64Matrix MPCOperator::MPC_Dot_Mul(const si64Matrix &A, const si64Matrix &B) {
  if (A.cols() != B.cols() || A.rows() != B.rows())
    throw std::runtime_error(LOCATION);

  si64Matrix ret(A.rows(), A.cols());
  eval.asyncDotMul(runtime, A, B, ret).get();
  return ret;
}

si64 MPCOperator::MPC_Mul_Const(const i64 &constInt, const si64 &sharedInt) {
  si64 ret;
  eval.asyncConstMul(constInt, sharedInt, ret);
  return ret;
}

si64Matrix MPCOperator::MPC_Mul_Const(const i64 &constInt,
                                      const si64Matrix &sharedIntMatrix) {
  si64Matrix ret(sharedIntMatrix.rows(), sharedIntMatrix.cols());
  eval.asyncConstMul(constInt, sharedIntMatrix, ret);
  return ret;
}
} // namespace primihub
