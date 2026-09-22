-- External/imported waveform channels (public datasets, auxiliary leads).
-- config.channels is also synced from vitalq.core.channels by vitalq-migrate;
-- this migration covers deployments that apply SQL directly.

insert into config.channels (channel_id, sensor_type, unit, sample_role) values
  ('resp.waveform', 'external', null, 'waveform'),
  ('ecg.ii',        'external', null, 'waveform'),
  ('ecg.v',         'external', null, 'waveform'),
  ('ecg.avr',       'external', null, 'waveform')
on conflict (channel_id) do nothing;
