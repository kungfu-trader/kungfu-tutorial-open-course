#include <kungfu/wingchun/extension.h>
#include <kungfu/wingchun/strategy/context.h>
#include <kungfu/wingchun/strategy/runtime.h>
#include <kungfu/wingchun/strategy/strategy.h>
#include <kungfu/yijinjing/time.h>
#include <cmath>
#include <typeinfo>

#include <stdio.h>
#include <string>
#include <time.h>
#include <deque>
#include <fstream>
#include <numeric>
#include <map>

using namespace kungfu::longfist::enums;
using namespace kungfu::longfist::types;
using namespace kungfu::wingchun::strategy;
using namespace kungfu::yijinjing::data;
using namespace kungfu::yijinjing;

#define VOLUME_UNIT 1
#define TIME_21_NS 1260000000000
// #define ACCOUNT_ID "115696"
// #define SOURCE_INDEX "ctp"
#define ACCOUNT_ID "123456"
#define SOURCE_INDEX "sim"

#define AM0_START_TIME "09:00:00"
#define AM0_END_TIME   "10:15:00"
#define AM1_START_TIME "10:30:00"
#define AM1_END_TIME   "11:30:00"
#define PM0_START_TIME "13:30:00"
#define PM0_END_TIME   "15:00:00"
#define PM1_START_TIME "21:00:00"
#define PM1_END_TIME   "23:00:00"

using namespace ::std;

struct Tick
{
    int64_t time;
    double price;
};

struct TickDetal
{
    string instrument_id;
    double last_price;
    int64_t time_ms;
    string other_instrument_id;
    OrderStatus status;
};

struct TickPair
{
    string product_name;
    string first_instrument;
    string second_instrument;
    TickDetal first_detal;
    TickDetal second_detal;
    double gap;
    int64_t gap_time;
    double mean;
    double para;
    bool open_sign;
};

KUNGFU_MAIN_STRATEGY(KungfuStrategy101)
{

public:
    KungfuStrategy101() = default;
    ~KungfuStrategy101() = default;

    map<string, TickPair> para_map;
    map<string, deque<Tick>> mds_map;
    vector<string> tickers;
    string today_date;
    bool skip_md_sign;
    void pre_start(Context_ptr & context) override
    {
        init_sign(context);
        SPDLOG_INFO("preparing strategy");
        context->add_account(SOURCE_INDEX, ACCOUNT_ID);
        context->subscribe(SOURCE_INDEX, tickers, EXCHANGE_SHFE);
    }

    void post_start(Context_ptr & context) override
    {
        SPDLOG_INFO("post_start {}", time::strftime(context->now()));
        context->add_time_interval(60000000000, [this, context](kungfu::event_ptr e) -> void
                                   { this->count_call(context); });
    }

    void on_quote(Context_ptr & context, const Quote &quote, const location_ptr &location) override
    {
        if (skip_md_sign)
            return;
        int64_t secs = quote.data_time;
        string instrument_id = quote.instrument_id;
        auto t = mds_map.find(instrument_id);
        if (t != mds_map.end())
        {
            Tick tick = {secs, quote.last_price};
            mds_map[instrument_id].push_back(tick);
            string product = get_product(instrument_id);
            if (para_map.find(product) != para_map.end())
            {
                if ((instrument_id == para_map[product].first_instrument) and (secs > para_map[product].first_detal.time_ms))
                {
                    para_map[product].first_detal.last_price = quote.last_price;
                    para_map[product].first_detal.time_ms = secs;
                }
                if ((instrument_id == para_map[product].second_instrument) and (secs > para_map[product].second_detal.time_ms))
                {
                    para_map[product].second_detal.last_price = quote.last_price;
                    para_map[product].second_detal.time_ms = secs;
                }
                if ((para_map[product].first_detal.time_ms == para_map[product].second_detal.time_ms) and (para_map[product].first_detal.time_ms > para_map[product].gap_time))
                {
                    para_map[product].gap = para_map[product].first_detal.last_price - para_map[product].second_detal.last_price;
                    para_map[product].gap_time = para_map[product].first_detal.time_ms;
                    if ((para_map[product].mean != 0) && (para_map[product].gap - para_map[product].mean > para_map[product].para) && (!para_map[product].open_sign))
                    {
                        SPDLOG_INFO("on quote: {} {} {} {} {} {}", product, para_map[product].first_detal.time_ms, para_map[product].second_detal.time_ms, para_map[product].first_detal.last_price, para_map[product].second_detal.last_price, para_map[product].first_detal.last_price - para_map[product].second_detal.last_price);
                        SPDLOG_INFO("[sell_open]");
                        order(context, product, "sell_open");
                        para_map[product].open_sign = true;
                    }
                    if ((para_map[product].mean != 0) && (para_map[product].gap - para_map[product].mean < -para_map[product].para) && para_map[product].open_sign)
                    {
                        SPDLOG_INFO("on quote: {} {} {} {} {} {}", product, para_map[product].first_detal.time_ms, para_map[product].second_detal.time_ms, para_map[product].first_detal.last_price, para_map[product].second_detal.last_price, para_map[product].first_detal.last_price - para_map[product].second_detal.last_price);
                        SPDLOG_INFO("[buy_close]");
                        order(context, product, "buy_close");
                        para_map[product].open_sign = false;
                    }
                }
            }
        }
    }

    void on_order(Context_ptr & context, const Order &order, const location_ptr &location) override
    {
        SPDLOG_INFO("[RTN_ORDER]  (t){} (p){}  (v){} (v_tot){} (request_id){}", order.instrument_id, order.limit_price, order.volume, order.volume_left, order.order_id);
        if ((order.status == OrderStatus::Filled) or (order.status == OrderStatus::PartialFilledNotActive) or (order.status == OrderStatus::Cancelled))
        {
            string instrument = order.instrument_id;
            string product = get_product(instrument);
            if (para_map.find(product) != para_map.end())
            {
                if (instrument == para_map[product].first_instrument)
                    para_map[product].first_detal.status = order.status;
                else if (instrument == para_map[product].second_instrument)
                    para_map[product].second_detal.status = order.status;
                if ((para_map[product].first_detal.status != OrderStatus::Unknown) && (para_map[product].second_detal.status != OrderStatus::Unknown))
                {
                    if ((para_map[product].first_detal.status == OrderStatus::Filled) and (para_map[product].second_detal.status == OrderStatus::Filled))
                    {
                        SPDLOG_INFO("finish trade");
                        para_map[product].first_detal.status = OrderStatus::Unknown;
                        para_map[product].second_detal.status = OrderStatus::Unknown;
                    }
                    else if ((para_map[product].first_detal.status == OrderStatus::Filled) and (para_map[product].second_detal.status == OrderStatus::Cancelled))
                    {
                        if (para_map[product].open_sign)
                        {
                            uint64_t rid_2 = context->insert_order(para_map[product].second_instrument, get_exchange(para_map[product].second_instrument), SOURCE_INDEX, ACCOUNT_ID, para_map[product].second_detal.last_price+2, VOLUME_UNIT, PriceType::Limit, Side::Buy, Offset::Open, HedgeFlag::Speculation);
                        }
                        else
                        {
                            uint64_t rid_2 = context->insert_order(para_map[product].second_instrument, get_exchange(para_map[product].second_instrument), SOURCE_INDEX, ACCOUNT_ID, para_map[product].second_detal.last_price-2, VOLUME_UNIT, PriceType::Limit, Side::Sell, Offset::CloseToday, HedgeFlag::Speculation);
                        }
                    }
                    else if ((para_map[product].first_detal.status == OrderStatus::Cancelled) and (para_map[product].second_detal.status == OrderStatus::Filled))
                    {
                        if (para_map[product].open_sign)
                        {
                            uint64_t rid_2 = context->insert_order(para_map[product].first_instrument, get_exchange(para_map[product].first_instrument), SOURCE_INDEX, ACCOUNT_ID, para_map[product].first_detal.last_price-2, VOLUME_UNIT, PriceType::Limit, Side::Sell, Offset::Open, HedgeFlag::Speculation);
                        }
                        else
                        {
                            uint64_t rid_2 = context->insert_order(para_map[product].first_instrument, get_exchange(para_map[product].first_instrument), SOURCE_INDEX, ACCOUNT_ID, para_map[product].first_detal.last_price+2, VOLUME_UNIT, PriceType::Limit, Side::Buy, Offset::CloseToday, HedgeFlag::Speculation);
                        }
                    }
                    else if ((para_map[product].first_detal.status == OrderStatus::Cancelled) and (para_map[product].second_detal.status == OrderStatus::Cancelled))
                    {
                        SPDLOG_INFO("failed trade");
                        para_map[product].first_detal.status = OrderStatus::Unknown;
                        para_map[product].second_detal.status = OrderStatus::Unknown;
                    }
                }
            }
        }
    };

    void on_trade(Context_ptr & context, const Trade &trade, const location_ptr &location) override
    {
        SPDLOG_INFO("[RTN_ORDER]  (t){} (p){}  (v){} (request_id){}", trade.instrument_id, trade.price, trade.volume, trade.order_id);
    };

private:
    void init_sign(Context_ptr & context)
    {
        SPDLOG_INFO("init_sign {}", time::strftime(context->now()));
        today_date = (time::strftime(context->now())).substr(0, 11);
        TickDetal td1 = {"rb2305", 0, 0, "rb2307", OrderStatus::Unknown};
        TickDetal td2 = {"rb2307", 0, 0, "rb2305", OrderStatus::Unknown};
        TickPair tp = {"rb", "rb2305", "rb2307", td1, td2, 0, 0, 0, 1.8, false};
        para_map["rb"] = tp;
        tickers = {"rb2305", "rb2307"};
        mds_map["rb2305"] = {};
        mds_map["rb2307"] = {};

        skip_md_sign = false;
        int64_t now = context->now();
        trade_start_call(context);
        if (now < get_time_stamp(today_date + AM0_START_TIME))
        {
            trade_end_call(context);
            context->add_timer(get_time_stamp(today_date + AM0_START_TIME), [this, context](kungfu::event_ptr e) -> void
                               { this->trade_start_call(context); });
        }   
        if (now < get_time_stamp(today_date + AM0_END_TIME))
            context->add_timer(get_time_stamp(today_date + AM0_END_TIME), [this, context](kungfu::event_ptr e) -> void
                               { this->trade_end_call(context); });
        if (now < get_time_stamp(today_date + AM1_START_TIME))
        {
            if (now >= get_time_stamp(today_date + AM0_END_TIME))
                trade_end_call(context);
            context->add_timer(get_time_stamp(today_date + AM1_START_TIME), [this, context](kungfu::event_ptr e) -> void
                               { this->trade_start_call(context); });
        }  
        if (now < get_time_stamp(today_date + AM1_END_TIME))
            context->add_timer(get_time_stamp(today_date + AM1_END_TIME), [this, context](kungfu::event_ptr e) -> void
                               { this->trade_end_call(context); });
        if (now < get_time_stamp(today_date + PM0_START_TIME))
        {
            if (now >= get_time_stamp(today_date + AM1_END_TIME))
                trade_end_call(context);
            context->add_timer(get_time_stamp(today_date + PM0_START_TIME), [this, context](kungfu::event_ptr e) -> void
                               { this->trade_start_call(context); });
        }  
        if (now < get_time_stamp(today_date + PM0_END_TIME))
            context->add_timer(get_time_stamp(today_date + PM0_END_TIME), [this, context](kungfu::event_ptr e) -> void
                               { this->trade_end_call(context); });                    
        if (now < get_time_stamp(today_date + PM1_START_TIME))
        {
            if (now >= get_time_stamp(today_date + PM0_END_TIME))
                trade_end_call(context);
            context->add_timer(get_time_stamp(today_date + PM1_START_TIME), [this, context](kungfu::event_ptr e) -> void
                               { this->trade_start_call(context); });
        }  
        if (now < get_time_stamp(today_date + PM1_END_TIME))
            context->add_timer(get_time_stamp(today_date + PM1_END_TIME), [this, context](kungfu::event_ptr e) -> void
                               { this->trade_end_call(context); });
    };

    void count_call(Context_ptr context)
    {
        SPDLOG_INFO("[count_call] {}", time::strftime(context->now()));
        for (auto iter = para_map.begin(); iter != para_map.end(); iter++)
        {
            string first_instrument = iter->second.first_instrument;
            string second_instrument = iter->second.second_instrument;
            if ((mds_map[first_instrument].size() > 2) and (mds_map[second_instrument].size() > 2) and (mds_map[first_instrument].back().time - mds_map[first_instrument].front().time > TIME_21_NS) and (mds_map[second_instrument].back().time - mds_map[second_instrument].front().time > TIME_21_NS))
            {
                for (auto iter_in : mds_map[first_instrument])
                {
                    if (mds_map[first_instrument].back().time - iter_in.time > TIME_21_NS)
                        mds_map[first_instrument].pop_front();
                    else
                        break;
                }
                for (auto iter_in : mds_map[second_instrument])
                {
                    if (mds_map[second_instrument].back().time - iter_in.time > TIME_21_NS)
                        mds_map[second_instrument].pop_front();
                    else
                        break;
                }
                double sum_first = 0;
                for (auto iter_in : mds_map[first_instrument])
                {
                    sum_first = sum_first + iter_in.price;
                }
                double avg_first = sum_first / mds_map[first_instrument].size();

                double sum_second = 0;
                for (auto iter_in : mds_map[second_instrument])
                {
                    sum_second = sum_second + iter_in.price;
                }
                double avg_second = sum_second / mds_map[second_instrument].size();
                iter->second.mean = avg_first - avg_second;
                SPDLOG_INFO("[count_call] {} {} {}", first_instrument, second_instrument, iter->second.mean);
            }
        }
    };

    string get_product(string instrument_id)
    {
        string product = instrument_id;
        size_t i = 0;
        size_t len = product.length();
        while (i < len)
        {
            if (!isalpha(product[i]))
            {
                product.erase(i, 1);
                len--;
            }
            else
                i++;
        }
        return product;
    };

    string get_exchange(const string &instrument_id)
    {
        string product = get_product(instrument_id);

        if (product == "c" || product == "cs" || product == "a" || product == "b" || product == "m" || product == "y" ||
            product == "p" || product == "fb" || product == "bb" || product == "jd" || product == "l" || product == "v" ||
            product == "pp" || product == "j" || product == "jm" || product == "i")
        {
            return EXCHANGE_SZE;
        }
        else if (product == "WH" || product == "PM" || product == "CF" || product == "SR" || product == "TA" || product == "OI" ||
                 product == "RI" || product == "MA" || product == "FG" || product == "RS" || product == "RM" || product == "ZC" ||
                 product == "JR" || product == "LR" || product == "SM" || product == "SF" || product == "CY" || product == "AP")
        {
            return EXCHANGE_CZCE;
        }
        else if (product == "cu" || product == "al" || product == "zn" || product == "pb" || product == "ni" || product == "sn" ||
                 product == "au" || product == "ag" || product == "rb" || product == "wr" || product == "hc" || product == "fu" ||
                 product == "bu" || product == "ru")
        {
            return EXCHANGE_SHFE;
        }
        else if (product == "IF" || product == "IC" || product == "IH" || product == "TF" || product == "T")
        {
            return EXCHANGE_CFFEX;
        }
        else
        {
            return "";
        }
    };

    void cancel_call(Context_ptr context, uint64_t rid)
    {
        uint64_t c_rid = context->cancel_order(rid);
        SPDLOG_INFO("[cancel_order] {}", c_rid);
    };

    void trade_end_call(Context_ptr context)
    {
        SPDLOG_INFO("[trade_end_call] {}", time::strftime(context->now()));
        skip_md_sign = true;
    };

    void trade_start_call(Context_ptr context)
    {
        SPDLOG_INFO("[trade_start_call] {}", time::strftime(context->now()));
        skip_md_sign = false;
        int64_t pre_time = 0;
        for (auto iter = mds_map.begin(); iter != mds_map.end(); iter++)
        {
            if ((iter->second.size() > 1) and (pre_time < iter->second.back().time))
                pre_time = iter->second.back().time;
        }
        if (pre_time != 0)
        {
            int64_t gap = context->now() - pre_time;
            for (auto iter = mds_map.begin(); iter != mds_map.end(); iter++)
            {
                for (auto iter_in : iter->second)
                {
                    iter_in.time = iter_in.time + gap;
                }
            }
        }
    };

    void order(Context_ptr & context, string product, string type)
    {
        if (type == "sell_open")
        {
            uint64_t rid_1 = context->insert_order(para_map[product].first_instrument, get_exchange(para_map[product].first_instrument), SOURCE_INDEX, ACCOUNT_ID, para_map[product].first_detal.last_price, VOLUME_UNIT, PriceType::Limit, Side::Sell, Offset::Open, HedgeFlag::Speculation);
            uint64_t rid_2 = context->insert_order(para_map[product].second_instrument, get_exchange(para_map[product].second_instrument), SOURCE_INDEX, ACCOUNT_ID, para_map[product].second_detal.last_price, VOLUME_UNIT, PriceType::Limit, Side::Buy, Offset::Open, HedgeFlag::Speculation);
            context->add_timer(context->now() + 500000000, [this, context, rid_1](kungfu::event_ptr e) -> void
                               { this->cancel_call(context, rid_1); });
            context->add_timer(context->now() + 500000000, [this, context, rid_2](kungfu::event_ptr e) -> void
                               { this->cancel_call(context, rid_2); });
        }
        else if (type == "buy_close")
        {
            uint64_t rid_1 = context->insert_order(para_map[product].first_instrument, get_exchange(para_map[product].first_instrument), SOURCE_INDEX, ACCOUNT_ID, para_map[product].first_detal.last_price, VOLUME_UNIT, PriceType::Limit, Side::Buy, Offset::CloseToday, HedgeFlag::Speculation);
            uint64_t rid_2 = context->insert_order(para_map[product].second_instrument, get_exchange(para_map[product].second_instrument), SOURCE_INDEX, ACCOUNT_ID, para_map[product].second_detal.last_price, VOLUME_UNIT, PriceType::Limit, Side::Sell, Offset::CloseToday, HedgeFlag::Speculation);
            context->add_timer(context->now() + 500000000, [this, context, rid_1](kungfu::event_ptr e) -> void
                               { this->cancel_call(context, rid_1); });
            context->add_timer(context->now() + 500000000, [this, context, rid_2](kungfu::event_ptr e) -> void
                               { this->cancel_call(context, rid_2); });
        }
    };

    int64_t get_time_stamp(string time_str, string format = "%Y-%m-%d %H:%M:%S")
    {
        struct tm tm_;
        int year, month, day, hour, minute, second;
        sscanf(time_str.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
        tm_.tm_year = year - 1900;
        tm_.tm_mon = month - 1;
        tm_.tm_mday = day;
        tm_.tm_hour = hour;
        tm_.tm_min = minute;
        tm_.tm_sec = second;
        tm_.tm_isdst = 0;
        time_t timeStamp = mktime(&tm_);
        int64_t nano_time = 1000000000 * timeStamp;
        return nano_time;
    };
};
