# AITerm 0.9.14-zeta-4b

Based on the tested zeta-4a source archive. Adds read-only individual host selection and filtering. No database schema, collection schedule, or AI provider changes.

## Commands

- `/hosts dump` — all hosts, as before.
- `/hosts dump xen.gigaware.lan` — exact hostname (case insensitive).
- `/hosts dump *.gigaware.lan` — glob matching (`*` and `?`).
- `/hosts dump source:snmp` — hosts with SNMP membership.
- `/hosts dump source:ssh xen*` — combine source and hostname filters.
- Sources: local, snmp, ssh, systemd, database. Source filters select hosts, not individual fields; all observations for matching hosts remain visible.

The shared formatter snapshots host records and SNMP observations under the existing mutex, then filters outside the lock. Original host indexes are retained for SNMP association. Remote SSH details are included in dumps. No polling or provider requests are triggered.

The Host Analysis Manager has a hostname search (substring/glob) and source dropdown. Existing selection is retained when still visible; otherwise the first visible host is selected, or details are cleared if no match. Filtering does not change schedules or stored data.

## Smoke tests

Build with `make clean && make`. Check version, run the commands above, test an unknown hostname and invalid source, compare SNMP OIDs with unfiltered dump, and verify GUI filters and selection after refresh. Confirm `/hosts db status` and history still work. No automatic AI reporting or change detection is introduced.

Full build and runtime tests were not possible in the packaging environment (development dependencies unavailable).
