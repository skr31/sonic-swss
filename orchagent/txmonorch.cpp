# include "txmonorch.h"
#include "converter.h"
#include "timer.h"
#include "sai_serialize.h"

using namespace swss;

const uint32_t POLLING_INTERVAL = 1;
const uint32_t DEFAULT_TIME_PERIOD = 30;
const uint32_t DEFAULT_THRESHOLD = 200;
const std::string MONITORED_COUNTER = "SAI_PORT_STAT_ETHER_RX_OVERSIZE_PKTS";

TXMonOrch::TXMonOrch(DBConnector *db, const std::string tableName): Orch(db, tableName),
    m_countersDb(new DBConnector("COUNTERS_DB", 0)),
    m_countersTable(new Table(m_countersDb.get(), COUNTERS_TABLE)),
    m_countersMapTable(new Table(m_countersDb.get(), COUNTERS_PORT_NAME_MAP)),
    m_stateDb(new DBConnector("STATE_DB", 0)),
    m_stateTable(new Table(m_stateDb.get(), "TX_MONITOR_TABLE")),
    m_threshold(DEFAULT_THRESHOLD),
    m_resettingTimer(new SelectableTimer(timespec { .tv_sec = DEFAULT_TIME_PERIOD, .tv_nsec = 0 })),
    m_pollingTimer(new SelectableTimer(timespec { .tv_sec = POLLING_INTERVAL, .tv_nsec = 0 }))
    {
        auto execResetTimer = new ExecutableTimer(m_resettingTimer, this, "TX_MONITOR_TIMER");
        Orch::addExecutor(execResetTimer);
        m_resettingTimer->start();
        auto execPollTimer = new ExecutableTimer(m_pollingTimer, this, "POLLING_TIMER");
        Orch::addExecutor(execPollTimer);
        m_pollingTimer->start();

    }

void TXMonOrch::doTask(Consumer& consumer)
{
    SWSS_LOG_ENTER();

    auto &taskMap = consumer.m_toSync;
    auto table_name = consumer.getTableName();

    if (table_name != CFG_TX_MON_TABLE_NAME)
    {
        SWSS_LOG_ERROR("Invalid table %s", table_name.c_str());
    }
 
    auto it = taskMap.begin();
    while (it != taskMap.end())
    {
        KeyOpFieldsValuesTuple t = it->second;

        std::string key = kfvKey(t);
        std::string op = kfvOp(t);
        std::vector<FieldValueTuple> values = kfvFieldsValues(t);

        if (op == SET_COMMAND)
        {
            handleSetCommand(key, values);
        }
        else
        {
            SWSS_LOG_ERROR("Unknown operation type %s\n", op.c_str());
        }

        it = taskMap.erase(it);
    }
}

void TXMonOrch::handleSetCommand(const std::string& key, const std::vector<FieldValueTuple>& data)
{
    SWSS_LOG_ENTER();

    if (key == "GLOBAL")
    {

        for (std::pair<std::basic_string<char>, std::basic_string<char> > current: data)
        {
            std::string field = current.first;
            std::string value = current.second;

            if (field == "time_period")
            {
                setTimePeriod(value);
            }
            else if (field == "threshold")
            {
                setThreshold(value);
            }
        }
    }
    else
    {
        SWSS_LOG_WARN("Unsupported key: %s", key.c_str());
    }
}

void TXMonOrch::setTimePeriod(const std::string newTimePeriod)
{
    auto intervT = timespec { .tv_sec = static_cast<time_t>(to_uint<uint32_t>(newTimePeriod.c_str())), .tv_nsec = 0 };
    m_resettingTimer->setInterval(intervT);
    m_resettingTimer->reset();
}

void TXMonOrch::setThreshold(const std::string newThreshold)
{
    m_threshold = to_uint<uint32_t>(newThreshold.c_str());
}

void TXMonOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_ENTER();
    mapAliasesToPorts();

    for (auto const& port: m_aliasToPortMap)
    {
        std::string eth_name = port.first;
        std::string oid = port.second;
        std::vector<FieldValueTuple> counterTuples;
    
        bool exist = m_countersTable->get(oid, counterTuples);
        if (exist)
        {
            for (std::pair<std::basic_string<char>, std::basic_string<char> > current: counterTuples)
            {
                std::string counter_type = current.first;
                std::string counter_value = current.second;

                if (counter_type == MONITORED_COUNTER)
                {
                    if (&timer == m_resettingTimer)
                    {
                        updateLastErrCount(eth_name, counter_value);
                    }
                    else if (&timer == m_pollingTimer)
                    {
                        checkMonitoredCounter(eth_name, counter_value);
                    }
                }
            }
        }
    }
}

void TXMonOrch::updateLastErrCount(std::string eth_name, std::string counter_value)
{
    m_lastErrCounts[eth_name] = to_uint<uint32_t>(counter_value.c_str());
    std::vector<FieldValueTuple> fvs;
    fvs.emplace_back("status", "OK");
    m_stateTable->set(eth_name, fvs);
    
}

void TXMonOrch::checkMonitoredCounter(std::string eth_name, std::string counter_value)
{
    if (to_uint<uint32_t>(counter_value.c_str()) - m_lastErrCounts[eth_name] > m_threshold)
    {
        std::vector<FieldValueTuple> fvs;
        fvs.emplace_back("status", "NOT_OK");
        m_stateTable->set(eth_name, fvs);
    }
}

void TXMonOrch::mapAliasesToPorts()
{
    std::vector<FieldValueTuple> counterTuples;

    m_countersMapTable->get("", counterTuples);

    for (std::pair<std::basic_string<char>, std::basic_string<char> > current: counterTuples)
    {
        std::string eth_name = current.first;
        std::string oid = current.second;

        m_aliasToPortMap[eth_name] = oid;
    }
}