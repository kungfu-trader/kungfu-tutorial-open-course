#include <kungfu/wingchun/extension.h>
#include <kungfu/wingchun/strategy/context.h>
#include <kungfu/wingchun/strategy/runtime.h>
#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/yijinjing/time.h>
#include<cmath>
#include <typeinfo>


using namespace kungfu::longfist::enums;
using namespace kungfu::longfist::types;
using namespace kungfu::wingchun::strategy;
using namespace kungfu::yijinjing::data;
using namespace kungfu::yijinjing;

KUNGFU_MAIN_STRATEGY(KungfuStrategy101) {
public:
  KungfuStrategy101() = default;
  ~KungfuStrategy101() = default;

  std::string account = "123456";
  std::string source = "sim";
  std::vector<std::string> tickers{"000001"};
  std::string exchange = "SZE";
  int volumes = 2000;
  int times = 100;
  int number = 10;
  int time_steps = times / number;
  int insert_volume = volumes / number;
  int complete_number = 0;
  int filled_volume = 0;
  std::vector<uint64_t> order_ids{};

  void pre_start(Context_ptr & context) override {
    SPDLOG_INFO("preparing strategy");
    context->add_account(source, account);
    context->subscribe("sim", tickers, exchange);
  }

  void post_start(Context_ptr & context) override {
    SPDLOG_INFO("post_start");
    auto oid = context->insert_order("000001", exchange, source, account, 1, insert_volume,
                          PriceType::Fok, Side::Buy, Offset::Open, HedgeFlag::Speculation);
    SPDLOG_INFO("auto oid {} , {}",oid,complete_number);
    auto &runtime = dynamic_cast<RuntimeContext &>(*context);
    auto &bookkeeper = runtime.get_bookkeeper();
    // 获取账户信息
    auto td_location = location::make_shared(mode::LIVE, category::TD, "sim", "123456", std::make_shared<locator>());
    kungfu::wingchun::book::Book_ptr book = bookkeeper.get_book(td_location->uid);
    SPDLOG_INFO("get_long_position : {}", book->get_long_position({"SSE"},"600000").to_string());
    }


  void on_quote(Context_ptr & context, const Quote &quote, const location_ptr &location) override {
    SPDLOG_INFO("on quote: {}", quote.to_string());
    for (int i = 0; i < quote.bid_price.size(); i++) {
      SPDLOG_INFO("quote.bid_price.size() : {}", quote.bid_price.size());
      SPDLOG_INFO("i : {}", i);
      SPDLOG_INFO("bid_price ------ : {}", quote.bid_price[i]);
    };
  }

  void on_order(Context_ptr &context, const Order &order,const location_ptr &location) override{
    SPDLOG_INFO("on order: {}", order.to_string());
    SPDLOG_INFO("order_id : {}", typeid(order.order_id).name());
    SPDLOG_INFO("external_id : {}", typeid(order.external_id).name());
  };


  void on_trade(Context_ptr &context, const Trade &trade,const location_ptr &location) override{
	  SPDLOG_INFO("on trade: {}", trade.to_string());
	  SPDLOG_INFO("order_id : {}", typeid(trade.order_id).name());
    SPDLOG_INFO("external_id : {}", typeid(trade.external_id).name());
    SPDLOG_INFO("trade_id : {}", typeid(trade.trade_id).name());

};

};
