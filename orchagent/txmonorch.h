#pragma once

#include "orch.h"

using namespace swss;

class TXMonOrch: public Orch
{

public:

    TXMonOrch(DBConnector *db, const std::string table_name);
    virtual void doTask(SelectableTimer &timer);
    virtual void doTask(Consumer &consumer);

private:
    void handleSetCommand(const std::string& key, const std::vector<FieldValueTuple>& data);
    void setTimePeriod(const std::string newTimePeriod);
    void setThreshold(const std::string newThreshold);
    void checkMonitoredCounter(std::string eth_name, std::string counter_value);
    void updateLastErrCount(std::string eth_name, std::string counter_value);
    void mapAliasesToPorts();

    uint32_t m_threshold;
    std::shared_ptr<swss::DBConnector> m_countersDb = nullptr;
    std::shared_ptr<swss::Table> m_countersTable = nullptr;
    std::shared_ptr<swss::Table> m_countersMapTable = nullptr;
    std::shared_ptr<swss::DBConnector> m_stateDb = nullptr;
    std::shared_ptr<swss::Table> m_stateTable = nullptr;

    SelectableTimer *m_resettingTimer = nullptr;
    SelectableTimer *m_pollingTimer = nullptr;
    std::map<std::basic_string<char>, std::basic_string<char>> m_aliasToPortMap;
    std::map<std::basic_string<char>, uint32_t> m_lastErrCounts;
};