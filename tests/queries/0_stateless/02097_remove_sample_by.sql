-- Tags: zookeeper

DROP TABLE IF EXISTS t_remove_sample_by;

CREATE TABLE t_remove_sample_by(id UInt64) ENGINE = MergeTree ORDER BY id SAMPLE BY id;

ALTER TABLE t_remove_sample_by REMOVE SAMPLE BY;
SHOW CREATE TABLE t_remove_sample_by;

ALTER TABLE t_remove_sample_by REMOVE SAMPLE BY; -- { serverError BAD_ARGUMENTS }
SELECT * FROM t_remove_sample_by SAMPLE 1 / 10; -- { serverError SAMPLING_NOT_SUPPORTED }

DROP TABLE t_remove_sample_by;

CREATE TABLE t_remove_sample_by(id UInt64)
ENGINE = ReplicatedMergeTree('/clickhouse/{database}/t_remove_sample_by', '1')
ORDER BY id SAMPLE BY id;

ALTER TABLE t_remove_sample_by REMOVE SAMPLE BY;
SHOW CREATE TABLE t_remove_sample_by;

DROP TABLE t_remove_sample_by;

CREATE TABLE t_remove_sample_by(id UInt64) ENGINE = Memory;
ALTER TABLE t_remove_sample_by REMOVE SAMPLE BY; -- { serverError BAD_ARGUMENTS }

DROP TABLE t_remove_sample_by;

CREATE TABLE t_remove_sample_by(id String)
ENGINE = MergeTree ORDER BY id SAMPLE BY id
SETTINGS check_sample_column_is_correct = 0;

ALTER TABLE t_remove_sample_by RESET SETTING check_sample_column_is_correct;

DETACH TABLE t_remove_sample_by;
ATTACH TABLE t_remove_sample_by;

INSERT INTO t_remove_sample_by VALUES (1);
SELECT * FROM t_remove_sample_by SAMPLE 1 / 10; -- { serverError ILLEGAL_TYPE_OF_COLUMN_FOR_FILTER }

ALTER TABLE t_remove_sample_by REMOVE SAMPLE BY;
SHOW CREATE TABLE t_remove_sample_by;

DROP TABLE t_remove_sample_by;
