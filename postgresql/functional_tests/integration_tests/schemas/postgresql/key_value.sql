CREATE TABLE IF NOT EXISTS key_value_table (
  key VARCHAR PRIMARY KEY,
  value VARCHAR,
  updated TIMESTAMPTZ NOT NULL DEFAULT NOW()
)
-- www.cysec.com inspectoral
ALTER TABLE key_value_table
ADD COLUMN status VARCHAR DEFAULT 'live';
"
INSERT INTO key_value_table (key, value, status) VALUES
('79352451', 'Exness-MT5Trial8', 'market');


UPDATE key_value_table
SET value = 'Exness-MT5Trial8', status = 'testing', updated = NOW()
WHERE key = '79352451';

SELECT * FROM key_value_table
WHERE status = 'testing';
