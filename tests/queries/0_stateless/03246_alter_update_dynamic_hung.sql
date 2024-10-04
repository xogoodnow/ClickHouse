SET allow_experimental_dynamic_type = 1;
DROP TABLE IF EXISTS t0;
CREATE TABLE t0 (c0 Int) ENGINE = MergeTree() ORDER BY tuple();
INSERT INTO t0 (c0) VALUES (1);
ALTER TABLE t0 UPDATE c0 = EXISTS (SELECT 1 FROM t1 CROSS JOIN t0) WHERE 1;
ALTER TABLE t0 MODIFY COLUMN c0 Dynamic; --{serverError UNFINISHED}
DROP TABLE t0;
