CREATE TABLE IF NOT EXISTS key_value_table (
  key VARCHAR PRIMARY KEY,
  value VARCHAR,
  updated TIMESTAMPTZ NOT NULL DEFAULT NOW()
)
SELECT * FROM key_value_table;
UPDATE key_value_table
SET value = 'new_value_for_key_1', updated = NOW()
WHERE key = 'example_key_1';
