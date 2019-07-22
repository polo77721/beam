// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "wallet/base_transaction.h"
#include "wallet/base_tx_builder.h"
#include "common.h"

#include "second_side.h"

namespace beam::wallet
{
    TxParameters InitNewSwap(const WalletID& myID, Amount amount, Amount fee, AtomicSwapCoin swapCoin,
        Amount swapAmount, SwapSecondSideChainType chainType, bool isBeamSide = true,
        Height lifetime = kDefaultTxLifetime, Height responseTime = kDefaultTxResponseTime);

    TxParameters CreateSwapParameters();

    TxParameters AcceptSwapParameters(const TxParameters& initialParameters, const WalletID& myID);

    class SecondSideFactoryNotRegisteredException : public std::runtime_error
    {
    public:
        explicit SecondSideFactoryNotRegisteredException()
            : std::runtime_error("second side factory is not registered")
        {
        }

    };

    class ISecondSideFactory
    {
    public:
        using Ptr = std::shared_ptr<ISecondSideFactory>;

        virtual SecondSide::Ptr CreateSecondSide(BaseTransaction& tx, bool isBeamSide) = 0;
    };

    template<typename BridgeSide, typename Bridge, typename Settings>
    class SecondSideFactory : public ISecondSideFactory
    {
    public:
        SecondSideFactory(typename Bridge::Ptr bridge, typename Settings::Ptr settings)
            : m_bridge{ bridge }
            , m_settings{ settings }
        {

        }
    private:
        SecondSide::Ptr CreateSecondSide(BaseTransaction& tx, bool isBeamSide) override
        {
            return std::make_shared<BridgeSide>(tx, m_bridge, m_settings, isBeamSide);
        }
    private:
        typename Bridge::Ptr m_bridge;
        typename Settings::Ptr m_settings;
    };

    template<typename BridgeSide, typename Bridge, typename Settings>
    ISecondSideFactory::Ptr MakeSecondSideFactory(typename Bridge::Ptr bridge, typename Settings::Ptr settings)
    {
        return std::make_shared<SecondSideFactory<BridgeSide, Bridge, Settings>>(bridge, settings);
    }

    class LockTxBuilder;

    class AtomicSwapTransaction : public BaseTransaction
    {
        enum class SubTxState : uint8_t
        {
            Initial,
            Invitation,
            Constructed
        };

        class UninitilizedSecondSide : public std::exception
        {
        };

        class ISecondSideProvider
        {
        public:
            virtual SecondSide::Ptr GetSecondSide(BaseTransaction& tx) = 0;
        };

        class WrapperSecondSide
        {
        public:
            WrapperSecondSide(ISecondSideProvider& gateway, BaseTransaction& tx);
            SecondSide::Ptr operator -> ();

        private:
            ISecondSideProvider& m_gateway;
            BaseTransaction& m_tx;
            SecondSide::Ptr m_secondSide;
        };

    public:
        enum class State : uint8_t
        {
            Initial,
            Invitation,

            BuildingBeamLockTX,
            BuildingBeamRefundTX,
            BuildingBeamRedeemTX,

            HandlingContractTX,
            SendingRefundTX,
            SendingRedeemTX,

            SendingBeamLockTX,
            SendingBeamRefundTX,
            SendingBeamRedeemTX,

            Cancelled,

            CompleteSwap,
            Failed,
            Refunded
        };

    public:

        struct SwapConditions
        {
            Amount beamAmount = 0;
            Amount swapAmount = 0;
            AtomicSwapCoin swapCoin;
            bool isBeamSide = false;
            SwapSecondSideChainType sideChainType;

            bool operator== (const SwapConditions& other)
            {
                return beamAmount == other.beamAmount &&
                    swapAmount == other.swapAmount &&
                    swapCoin == other.swapCoin &&
                    isBeamSide == other.isBeamSide &&
                    sideChainType == other.sideChainType;
            }
        };
        
        class Creator : public BaseTransaction::Creator
                      , public ISecondSideProvider
        {
        public:
            //Creator(std::vector<SwapConditions>& swapConditions);
            void RegisterFactory(AtomicSwapCoin coinType, ISecondSideFactory::Ptr factory);
        private:
            BaseTransaction::Ptr Create(INegotiatorGateway& gateway
                                      , IWalletDB::Ptr walletDB
                                      , IPrivateKeyKeeper::Ptr keyKeeper
                                      , const TxID& txID) override;
            bool CanCreate(const TxParameters& parameters) override;

            SecondSide::Ptr GetSecondSide(BaseTransaction& tx) override;
        private:
            //std::vector<SwapConditions>& m_swapConditions;
            std::map<AtomicSwapCoin, ISecondSideFactory::Ptr> m_factories;
        };

        AtomicSwapTransaction(INegotiatorGateway& gateway
                            , WalletDB::Ptr walletDB
                            , IPrivateKeyKeeper::Ptr keyKeeper
                            , const TxID& txID
                            , ISecondSideProvider& secondSideProvider);

        void Cancel() override;

        bool Rollback(Height height) override;

    private:
        void SetNextState(State state);

        TxType GetType() const override;
        State GetState(SubTxID subTxID) const;
        SubTxState GetSubTxState(SubTxID subTxID) const;
        Amount GetWithdrawFee() const;
        void UpdateImpl() override;
        void RollbackTx() override;
        void NotifyFailure(TxFailureReason) override;
        void OnFailed(TxFailureReason reason, bool notify) override;
        bool CheckExpired() override;
        bool CheckExternalFailures() override;
        void SendInvitation();
        void SendExternalTxDetails();
        void SendLockTxInvitation(const LockTxBuilder& lockBuilder);
        void SendLockTxConfirmation(const LockTxBuilder& lockBuilder);

        void SendSharedTxInvitation(const BaseTxBuilder& builder);
        void ConfirmSharedTxInvitation(const BaseTxBuilder& builder);


        SubTxState BuildBeamLockTx();
        SubTxState BuildBeamWithdrawTx(SubTxID subTxID, Transaction::Ptr& resultTx);
        bool CompleteBeamWithdrawTx(SubTxID subTxID);
                
        bool SendSubTx(Transaction::Ptr transaction, SubTxID subTxID);

        bool IsBeamLockTimeExpired() const;

        // wait SubTX in BEAM chain(request kernel proof), returns true if got kernel proof
        bool CompleteSubTx(SubTxID subTxID);

        bool GetKernelFromChain(SubTxID subTxID) const;

        Amount GetAmount() const;
        bool IsSender() const;
        bool IsBeamSide() const;

        void OnSubTxFailed(TxFailureReason reason, SubTxID subTxID, bool notify = false);
        void CheckSubTxFailures();
        void ExtractSecretPrivateKey();

        mutable boost::optional<bool> m_IsBeamSide;
        mutable boost::optional<bool> m_IsSender;
        mutable boost::optional<beam::Amount> m_Amount;

        Transaction::Ptr m_LockTx;
        Transaction::Ptr m_WithdrawTx;

        WrapperSecondSide m_secondSide;
    };
}
