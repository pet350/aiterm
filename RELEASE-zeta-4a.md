# AITerm 0.9.14-zeta-4a

Base: supplied 0.9.14-zeta-3a archive. Additive persistence only; no AI reporting changes.

- Existing SSH latest/history and schedule tables remain intact.
- Dedicated DB worker creates two additional InnoDB tables automatically: `host_analysis_observation_latest` and `host_analysis_observation_history`. JSON text payloads retain collector-specific fields without changing the SSH schema.
- Local Linux and systemd snapshots are queued after releasing the host lock. SNMP snapshots are queued on full refresh (periodic/manual), not every ticker update. Original SNMP sample timestamps are preserved; repeated imports of the same host/OID/timestamp update the same history row. This is sampled history, not a record of every SNMP poll.
- Latest observations retain last successful data; systemd failures are recorded in history with error text. Inventory rows are registered without changing SSH schedules. History cleanup includes the new tables (30 days successful / 90 days failed, bounded batches).
- `/hosts history HOST HOURS` includes the new observation history, with the existing row limit applied separately to SSH and other observations. `/hosts db status` shows persistence coverage.
- Existing SSH restoration is retained. New local/SNMP/systemd data is queryable from SQL and history but is not restored into live collector state. Disabled SNMP OIDs remain as historical/last-known records, not evidence of current availability.
- No new hardware collectors, CPU utilization, journal, or TCP collectors. Existing local collector can return zero for unavailable metrics; systemd errors are explicitly distinguished. No SNMP credentials are persisted by this addition.

## Verification on target

Back up the database, extract into a new directory, then `make clean && make`. Start AITerm and check `/hosts db status`, `/hosts refresh`, `/hosts history HOST 24`. Wait for SNMP samples and a full refresh. Verify SQL:

```sql
SELECT collector, COUNT(*) FROM host_analysis_observation_latest GROUP BY collector;
SELECT hostname, collector, metric, FROM_UNIXTIME(collected_at), payload
FROM host_analysis_observation_latest ORDER BY hostname, collector, metric;
SELECT collector, success, COUNT(*) FROM host_analysis_observation_history GROUP BY collector, success;
SELECT COUNT(*) FROM host_analysis_history;
```

Repeat refresh without a new SNMP sample and confirm no duplicate history key. Check systemd error behavior and existing SSH collection/history/schedules after restart. The DB account needs CREATE permissions for the new tables. Existing DB connection failure behavior is unchanged (no durable retry spool).

Build and live MariaDB tests have not been run in the packaging environment because development dependencies and a database server are unavailable.
