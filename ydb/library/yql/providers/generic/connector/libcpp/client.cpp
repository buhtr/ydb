#include <util/stream/file.h>
#include <yql/essentials/utils/log/log.h>

#include "client.h"

namespace NYql::NConnector {
    template <class TResponse>
    class TStreamIteratorImpl: public IStreamIterator<TResponse> {
    public:
        TStreamIteratorImpl(std::shared_ptr<TStreamer<TResponse>> stream)
            : Streamer_(stream)
                  {};

        TAsyncResult<TResponse> ReadNext() {
            Y_ENSURE(!Streamer_->IsFinished(), "Attempt to read from finished stream");
            return Streamer_->ReadNext(Streamer_);
        }

        std::shared_ptr<TStreamer<TResponse>> GetStreamer() {
            return Streamer_;
        }

    private:
        std::shared_ptr<TStreamer<TResponse>> Streamer_;
    };

    TListSplitsStreamIteratorDrainer::TPtr MakeListSplitsStreamIteratorDrainer(IListSplitsStreamIterator::TPtr&& iterator) {
        return std::make_shared<TListSplitsStreamIteratorDrainer>(std::move(iterator));
    }

    TReadSplitsStreamIteratorDrainer::TPtr MakeReadSplitsStreamIteratorDrainer(IReadSplitsStreamIterator::TPtr&& iterator) {
        return std::make_shared<TReadSplitsStreamIteratorDrainer>(std::move(iterator));
    }

    class TClientGRPC: public IClient {
    public:
        TClientGRPC() = delete;
        TClientGRPC(const TGenericConnectorConfig& config) {
            GrpcConfig_ = NYdbGrpc::TGRpcClientConfig();

            Y_ENSURE(config.GetEndpoint().host(), TStringBuilder() << "Empty host in TGenericConnectorConfig: " << config.DebugString());
            Y_ENSURE(config.GetEndpoint().port(), TStringBuilder() << "Empty port in TGenericConnectorConfig: " << config.DebugString());
            GrpcConfig_.Locator = TStringBuilder() << config.GetEndpoint().host() << ":" << config.GetEndpoint().port();
            GrpcConfig_.EnableSsl = config.GetUseSsl();

            YQL_CLOG(INFO, ProviderGeneric) << "Connector endpoint: " << (config.GetUseSsl() ? "grpcs" : "grpc") << "://" << GrpcConfig_.Locator;

            // Read content of CA cert
            TString rootCertData;
            if (config.GetSslCaCrt()) {
                rootCertData = TFileInput(config.GetSslCaCrt()).ReadAll();
            }

            GrpcConfig_.SslCredentials = grpc::SslCredentialsOptions{.pem_root_certs = rootCertData, .pem_private_key = "", .pem_cert_chain = ""};

            GrpcClient_ = std::make_unique<NYdbGrpc::TGRpcClientLow>();

            // FIXME: is it OK to use single connection during the client lifetime?
            GrpcConnection_ = GrpcClient_->CreateGRpcServiceConnection<NApi::Connector>(GrpcConfig_, NYdbGrpc::TTcpKeepAliveSettings {
                    // TODO configure hardcoded values
                    .Enabled = true,
                    .Idle = 30,
                    .Count = 5,
                    .Interval = 10
            });
        }

        virtual TDescribeTableAsyncResult DescribeTable(const NApi::TDescribeTableRequest& request, TDuration timeout = {}) override {
            return UnaryCall<NApi::TDescribeTableRequest, NApi::TDescribeTableResponse>(request, &NApi::Connector::Stub::AsyncDescribeTable, timeout);
        }

        virtual TListSplitsStreamIteratorAsyncResult ListSplits(const NApi::TListSplitsRequest& request, TDuration timeout = {}) override {
            return ServerSideStreamingCall<NApi::TListSplitsRequest, NApi::TListSplitsResponse>(request, &NApi::Connector::Stub::AsyncListSplits, timeout);
        }

        virtual TReadSplitsStreamIteratorAsyncResult ReadSplits(const NApi::TReadSplitsRequest& request, TDuration timeout = {}) override {
            return ServerSideStreamingCall<NApi::TReadSplitsRequest, NApi::TReadSplitsResponse>(request, &NApi::Connector::Stub::AsyncReadSplits, timeout);
        }

        ~TClientGRPC() {
            GrpcClient_->Stop(true);
        }

    private:
        template <class TService, class TRequest, class TResponse, template <typename TA, typename TB, typename TC> class TStream>
        using TStreamRpc =
            typename TStream<
                NApi::Connector::Stub,
                TRequest,
                TResponse>::TAsyncRequest;

        template <class TRequest, class TResponse>
        TAsyncResult<TResponse> UnaryCall(
            const TRequest& request,
            typename NYdbGrpc::TSimpleRequestProcessor<NApi::Connector::Stub, TRequest, TResponse>::TAsyncRequest rpc, TDuration timeout = {}) {
            auto context = GrpcClient_->CreateContext();
            if (!context) {
                throw yexception() << "Client is being shutdown";
            }

            auto promise = NThreading::NewPromise<TResult<TResponse>>();
            auto callback = [promise, context](NYdbGrpc::TGrpcStatus&& status, TResponse&& resp) mutable {
                promise.SetValue({std::move(status), std::move(resp)});
            };

            GrpcConnection_->DoRequest<TRequest, TResponse>(
                std::move(request),
                std::move(callback),
                rpc,
                { .Timeout = timeout },
                context.get());

            return promise.GetFuture();
        }

        template <class TRequest, class TResponse>
        TIteratorAsyncResult<IStreamIterator<TResponse>> ServerSideStreamingCall(
            const TRequest& request,
            TStreamRpc<NApi::Connector::Stub, TRequest, TResponse, NYdbGrpc::TStreamRequestReadProcessor> rpc,
            TDuration timeout = {}) {
            using TStreamProcessorPtr = typename NYdbGrpc::IStreamRequestReadProcessor<TResponse>::TPtr;
            using TStreamInitResult = std::pair<NYdbGrpc::TGrpcStatus, TStreamProcessorPtr>;

            auto promise = NThreading::NewPromise<TStreamInitResult>();

            auto context = GrpcClient_->CreateContext();
            if (!context) {
                throw yexception() << "Client is being shutdown";
            }

            GrpcConnection_->DoStreamRequest<TRequest, TResponse>(
                request,
                [context, promise](NYdbGrpc::TGrpcStatus&& status, TStreamProcessorPtr streamProcessor) mutable {
                    promise.SetValue({std::move(status), streamProcessor});
                },
                rpc,
                { .Timeout = timeout },
                context.get());

            // TODO: async handling YQ-2513
            auto result = promise.GetFuture().GetValueSync();

            auto status = result.first;
            auto streamProcessor = result.second;

            if (streamProcessor) {
                auto it = std::make_shared<TStreamIteratorImpl<TResponse>>(std::make_shared<TStreamer<TResponse>>(std::move(streamProcessor)));
                return NThreading::MakeFuture<TIteratorResult<IStreamIterator<TResponse>>>({std::move(status), std::move(it)});
            }

            return NThreading::MakeFuture<TIteratorResult<IStreamIterator<TResponse>>>({std::move(status), nullptr});
        }

    private:
        NYdbGrpc::TGRpcClientConfig GrpcConfig_;
        std::unique_ptr<NYdbGrpc::TGRpcClientLow> GrpcClient_;
        std::shared_ptr<NYdbGrpc::TServiceConnection<NApi::Connector>> GrpcConnection_;
    };

    
    class TConnectorService : public IClient {
    public:
        TConnectorService() = delete;

        TConnectorService(const TGenericConnectorConfig & config) : GatewayConfig_(config) {
        }

        ~TConnectorService() {
        }

    public: 
        virtual TDescribeTableAsyncResult DescribeTable(const NApi::TDescribeTableRequest& request, TDuration timeout = {}) override {
            auto c = createClient();
            auto future = c->DescribeTable(request, timeout);
            
            return future.Apply([c = std::move(c)](const NThreading::TFuture<TResult<NApi::TDescribeTableResponse>> & f) mutable {
                c.reset();
                Cerr << "Clear GRPC client for describe, count: " << c.use_count();;
                return f;
            });
        }

        virtual TListSplitsStreamIteratorAsyncResult ListSplits(const NApi::TListSplitsRequest& request, TDuration timeout = {}) override {
            auto c =  createClient();
            auto future = c->ListSplits(request, timeout);

            return wrapStreamResult<NApi::TListSplitsResponse>(future, c);
        }

        virtual TReadSplitsStreamIteratorAsyncResult ReadSplits(const NApi::TReadSplitsRequest& request, TDuration timeout = {}) override {
            auto c =  createClient();
            auto future = c->ReadSplits(request, timeout);

            return wrapStreamResult<NApi::TReadSplitsResponse>(future, c);
        }
 
    private: 
        IClient::TPtr createClient() {
            Cerr << "Create GRPC client for query";
            return std::make_shared<TClientGRPC>(GatewayConfig_);
        }

        template<class T>
        TIteratorAsyncResult<IStreamIterator<T>> wrapStreamResult(const TIteratorAsyncResult<IStreamIterator<T>> & future, IClient::TPtr client) {
            return future.Apply([client = std::move(client)](const NThreading::TFuture<TIteratorResult<IStreamIterator<T>>> & f) mutable {
                auto value = f.GetValue();
                auto it = std::make_shared<TWrapStream<T>>(value.Iterator, client);
                return NThreading::MakeFuture<TIteratorResult<IStreamIterator<T>>>({value.Status, it});
            });
        }
    private: 

        template<typename T>
        class TWrapStream final : public IStreamIterator<T> {
        public: 
            TWrapStream(const std::shared_ptr<IStreamIterator<T>> & r, const IClient::TPtr client) 
                : Stream_(r), Client_(client) {
        
            }

            TAsyncResult<T> ReadNext() {
                return Stream_->ReadNext();
            }

            ~TWrapStream() {
                Client_.reset();
                Cerr << "Clear GRPC client for a stream, count: " << Client_.use_count();
            }
        private: 
            std::shared_ptr<IStreamIterator<T>> Stream_;
            IClient::TPtr Client_;
        };

    private: 
        const TGenericConnectorConfig & GatewayConfig_;
    };


    IClient::TPtr MakeClientGRPC(const NYql::TGenericConnectorConfig& cfg) {
        return std::make_shared<TConnectorService>(cfg);
        //return std::make_shared<TClientGRPC>(cfg);
    }
} // namespace NYql::NConnector
