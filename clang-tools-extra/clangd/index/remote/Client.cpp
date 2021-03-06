//===--- Client.cpp ----------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <grpc++/grpc++.h>

#include "Client.h"
#include "Index.grpc.pb.h"
#include "index/Index.h"
#include "index/Serialization.h"
#include "marshalling/Marshalling.h"
#include "support/Logger.h"
#include "support/Trace.h"
#include "llvm/ADT/StringRef.h"

#include <chrono>

namespace clang {
namespace clangd {
namespace remote {
namespace {

class IndexClient : public clangd::SymbolIndex {
  template <typename RequestT, typename ReplyT>
  using StreamingCall = std::unique_ptr<grpc::ClientReader<ReplyT>> (
      remote::SymbolIndex::Stub::*)(grpc::ClientContext *, const RequestT &);

  template <typename RequestT, typename ReplyT, typename ClangdRequestT,
            typename CallbackT>
  bool streamRPC(ClangdRequestT Request,
                 StreamingCall<RequestT, ReplyT> RPCCall,
                 CallbackT Callback) const {
    bool FinalResult = false;
    trace::Span Tracer(RequestT::descriptor()->name());
    const auto RPCRequest = ProtobufMarshaller->toProtobuf(Request);
    grpc::ClientContext Context;
    std::chrono::system_clock::time_point Deadline =
        std::chrono::system_clock::now() + DeadlineWaitingTime;
    Context.set_deadline(Deadline);
    auto Reader = (Stub.get()->*RPCCall)(&Context, RPCRequest);
    ReplyT Reply;
    while (Reader->Read(&Reply)) {
      if (!Reply.has_stream_result()) {
        FinalResult = Reply.final_result();
        continue;
      }
      auto Response = ProtobufMarshaller->fromProtobuf(Reply.stream_result());
      if (!Response) {
        elog("Received invalid {0}", ReplyT::descriptor()->name());
        continue;
      }
      Callback(*Response);
    }
    SPAN_ATTACH(Tracer, "status", Reader->Finish().ok());
    return FinalResult;
  }

public:
  IndexClient(
      std::shared_ptr<grpc::Channel> Channel, llvm::StringRef ProjectRoot,
      std::chrono::milliseconds DeadlineTime = std::chrono::milliseconds(1000))
      : Stub(remote::SymbolIndex::NewStub(Channel)),
        ProtobufMarshaller(new Marshaller(/*RemoteIndexRoot=*/"",
                                          /*LocalIndexRoot=*/ProjectRoot)),
        DeadlineWaitingTime(DeadlineTime) {}

  void lookup(const clangd::LookupRequest &Request,
              llvm::function_ref<void(const clangd::Symbol &)> Callback) const {
    streamRPC(Request, &remote::SymbolIndex::Stub::Lookup, Callback);
  }

  bool
  fuzzyFind(const clangd::FuzzyFindRequest &Request,
            llvm::function_ref<void(const clangd::Symbol &)> Callback) const {
    return streamRPC(Request, &remote::SymbolIndex::Stub::FuzzyFind, Callback);
  }

  bool refs(const clangd::RefsRequest &Request,
            llvm::function_ref<void(const clangd::Ref &)> Callback) const {
    return streamRPC(Request, &remote::SymbolIndex::Stub::Refs, Callback);
  }

  // FIXME(kirillbobyrev): Implement this.
  void
  relations(const clangd::RelationsRequest &,
            llvm::function_ref<void(const SymbolID &, const clangd::Symbol &)>)
      const {}

  // IndexClient does not take any space since the data is stored on the
  // server.
  size_t estimateMemoryUsage() const { return 0; }

private:
  std::unique_ptr<remote::SymbolIndex::Stub> Stub;
  std::unique_ptr<Marshaller> ProtobufMarshaller;
  // Each request will be terminated if it takes too long.
  std::chrono::milliseconds DeadlineWaitingTime;
};

} // namespace

std::unique_ptr<clangd::SymbolIndex> getClient(llvm::StringRef Address,
                                               llvm::StringRef ProjectRoot) {
  const auto Channel =
      grpc::CreateChannel(Address.str(), grpc::InsecureChannelCredentials());
  Channel->GetState(true);
  return std::unique_ptr<clangd::SymbolIndex>(
      new IndexClient(Channel, ProjectRoot));
}

} // namespace remote
} // namespace clangd
} // namespace clang
