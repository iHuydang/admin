CREATE TABLE IF NOT EXISTS key_value_table (
  key VARCHAR PRIMARY KEY,
  value VARCHAR,
  updated TIMESTAMPTZ NOT NULL DEFAULT NOW()
)
INSERT INTO key_value_table (key, value) VALUES
('example_key_1', 'example_value_1'),
('example_key_2', 'example_value_2'),
('example_key_3', 'example_value_3');
SELECT * FROM key_value_table;
UPDATE key_value_table
SET value = 'new_value_for_key_1', updated = NOW()
WHERE key = 'example_key_1';
INSERT INTO key_value_table (key, value, status) VALUES
('market_account_1', 'account_#79352451', 'testing');
