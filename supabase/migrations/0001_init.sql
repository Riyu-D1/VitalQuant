-- VitalQ initial schema — docs/04-database-schema.md
-- Plain Postgres (15+/17): declarative partitioning, no TimescaleDB.

create schema if not exists config;
create schema if not exists raw;
create schema if not exists clean;
create schema if not exists quality;
create schema if not exists features;
create schema if not exists ml;
create schema if not exists meta;

do $$ begin
  create type data_class as enum ('real', 'synthetic', 'simulated');
exception when duplicate_object then null; end $$;

-- ── config / meta ────────────────────────────────────────────────────────────

create table if not exists config.hardware_revisions (
  revision        text primary key,
  description     text,
  profile         jsonb not null,
  created_at      timestamptz default now()
);

create table if not exists config.channels (
  channel_id   text primary key,
  sensor_type  text not null,
  unit         text,
  sample_role  text not null
);

insert into config.channels (channel_id, sensor_type, unit, sample_role) values
  ('ppg.red',          'optical',     'count', 'waveform'),
  ('ppg.ir',           'optical',     'count', 'waveform'),
  ('ppg.ch3',          'optical',     'count', 'waveform'),
  ('ppg.ch4',          'optical',     'count', 'waveform'),
  ('temp.object',      'temperature', 'degC',  'scalar'),
  ('temp.ambient',     'temperature', 'degC',  'scalar'),
  ('env.temperature',  'environment', 'degC',  'scalar'),
  ('env.humidity',     'environment', 'pctRH', 'scalar'),
  ('env.pressure',     'environment', 'hPa',   'scalar'),
  ('env.gas_resistance','environment','ohm',   'scalar'),
  ('motion.accel_x',   'motion',      'g',     'waveform'),
  ('motion.accel_y',   'motion',      'g',     'waveform'),
  ('motion.accel_z',   'motion',      'g',     'waveform'),
  ('motion.gyro_x',    'motion',      'dps',   'waveform'),
  ('motion.gyro_y',    'motion',      'dps',   'waveform'),
  ('motion.gyro_z',    'motion',      'dps',   'waveform'),
  ('contact.level',    'contact',     null,    'scalar'),
  ('sys.battery_v',    'system',      'V',     'scalar')
on conflict (channel_id) do nothing;

create table if not exists meta.devices (
  device_id         uuid primary key default gen_random_uuid(),
  label             text,
  hardware_revision text references config.hardware_revisions,
  api_key_hash      text not null,
  created_at        timestamptz default now(),
  revoked           boolean default false
);

create table if not exists meta.subjects (
  subject_id   uuid primary key default gen_random_uuid(),
  external_ref text,
  consent_ref  text,
  created_at   timestamptz default now()
);

create table if not exists meta.sessions (
  session_id       uuid primary key default gen_random_uuid(),
  device_id        uuid references meta.devices not null,
  subject_id       uuid references meta.subjects,
  firmware_version text not null,
  hardware_revision text references config.hardware_revisions not null,
  data_class       data_class not null default 'real',
  body_site        text,
  started_at       timestamptz not null,
  ended_at         timestamptz,
  notes            text
);
create index if not exists sessions_device_time on meta.sessions (device_id, started_at desc);

create table if not exists meta.labels (
  session_id uuid references meta.sessions not null,
  label_time timestamptz not null,
  kind       text not null,
  value      jsonb not null,
  provenance text not null,
  primary key (session_id, label_time, kind)
);

-- ── raw ──────────────────────────────────────────────────────────────────────

create table if not exists raw.ingest_batches (
  batch_id         uuid not null,
  device_id        uuid references meta.devices not null,
  session_id       uuid references meta.sessions not null,
  received_at      timestamptz default now(),
  clock_wall_time  timestamptz not null,
  clock_monotonic_us bigint not null,
  clock_offset_us  bigint,
  clock_drift_ppm  double precision,
  payload_bytes    int,
  primary key (device_id, batch_id)
);
create index if not exists ingest_batches_session on raw.ingest_batches (session_id);

create table if not exists raw.measurements_scalar (
  device_id    uuid not null,
  session_id   uuid not null,
  channel_id   text references config.channels not null,
  sample_time  timestamptz not null,
  device_time_us bigint not null,
  value        double precision,
  quality_flag smallint default 0,
  data_class   data_class not null,
  primary key (session_id, channel_id, sample_time)
) partition by range (sample_time);

create table if not exists raw.signal_windows (
  window_id        bigint generated always as identity,
  device_id        uuid not null,
  session_id       uuid not null,
  channel_id       text references config.channels not null,
  window_start     timestamptz not null,
  sample_rate_hz   real not null,
  n_samples        int not null,
  device_time_start_us bigint not null,
  samples          real[] not null,
  quality_flag     smallint default 0,
  data_class       data_class not null,
  primary key (session_id, channel_id, window_start, window_id)
) partition by range (window_start);

create table if not exists raw.spectral_frames (
  frame_id      bigint generated always as identity,
  device_id     uuid not null,
  session_id    uuid not null,
  sample_time   timestamptz not null,
  device_time_us bigint not null,
  read_group    smallint not null,
  channels      jsonb not null,
  gain_x        real,
  integ_time_ms real,
  illumination  text,
  sensor_temp_c real,
  quality_flag  smallint default 0,
  data_class    data_class not null,
  primary key (session_id, sample_time, read_group, frame_id)
) partition by range (sample_time);

create table if not exists raw.device_events (
  device_id  uuid not null,
  session_id uuid,
  event_time timestamptz not null,
  kind       text not null,
  detail     jsonb,
  primary key (device_id, event_time, kind)
);

-- monthly partitions for the range-partitioned raw tables
create or replace function raw.ensure_monthly_partitions(tbl regclass, months_ahead int default 3)
returns void language plpgsql as $$
declare
  m date;
begin
  for m in select generate_series(
      date_trunc('month', now())::date - interval '2 months',
      date_trunc('month', now())::date + make_interval(months => months_ahead),
      interval '1 month') loop
    execute format(
      'create table if not exists %s_%s partition of %s for values from (%L) to (%L)',
      tbl::text, to_char(m, 'YYYY_MM'), tbl::text, m, m + interval '1 month');
  end loop;
end $$;

select raw.ensure_monthly_partitions('raw.measurements_scalar', 12);
select raw.ensure_monthly_partitions('raw.signal_windows', 12);
select raw.ensure_monthly_partitions('raw.spectral_frames', 12);

create index if not exists signal_windows_lookup
  on raw.signal_windows (session_id, channel_id, window_start);
create index if not exists scalar_lookup
  on raw.measurements_scalar (session_id, channel_id, sample_time);

-- ── quality ──────────────────────────────────────────────────────────────────

create table if not exists quality.channel_quality (
  session_id   uuid not null,
  channel_id   text not null,
  window_start timestamptz not null,
  sqi          real not null check (sqi between 0 and 1),
  sqi_detail   jsonb,
  pipeline_version text not null,
  primary key (session_id, channel_id, window_start, pipeline_version)
);

create table if not exists quality.contact_state (
  session_id   uuid not null,
  window_start timestamptz not null,
  contact_quality real check (contact_quality between 0 and 1),
  motion_score    real,
  sensor_confidence real,
  pipeline_version text not null,
  primary key (session_id, window_start, pipeline_version)
);

-- ── features ─────────────────────────────────────────────────────────────────

create table if not exists features.windows (
  session_id       uuid not null,
  window_start     timestamptz not null,
  window_seconds   real not null,
  feature_set      text not null,
  pipeline_version text not null,
  values           jsonb not null,
  source_window_ids bigint[],
  data_class       data_class not null,
  primary key (session_id, window_start, feature_set, pipeline_version)
);

create table if not exists features.baselines (
  subject_id   uuid references meta.subjects not null,
  channel_id   text not null,
  stat         text not null,
  window_days  int not null,
  value        jsonb not null,
  computed_at  timestamptz default now(),
  pipeline_version text not null,
  primary key (subject_id, channel_id, stat, window_days, computed_at)
);

-- ── ml ───────────────────────────────────────────────────────────────────────

create table if not exists ml.experiments (
  experiment_id  uuid primary key default gen_random_uuid(),
  name           text not null,
  dataset_version text not null,
  feature_set    text not null,
  model_spec     jsonb not null,
  code_commit    text not null,
  created_at     timestamptz default now()
);

create table if not exists ml.models (
  model_id       uuid primary key default gen_random_uuid(),
  experiment_id  uuid references ml.experiments,
  artifact_uri   text,
  metrics        jsonb,
  train_window   tstzrange,
  created_at     timestamptz default now()
);

create table if not exists ml.predictions (
  session_id    uuid not null,
  window_start  timestamptz not null,
  model_id      uuid references ml.models not null,
  kind          text not null,
  value         real,
  uncertainty   real,
  detail        jsonb,
  primary key (session_id, window_start, model_id, kind)
);

-- ── access hardening (prototype RLS) ─────────────────────────────────────────
-- Ingest role writes raw only; raw is append-only for it (spec: never overwrite raw).
-- Supabase deployments: create role `vitalq_ingest` and grant INSERT/SELECT on raw.*.
-- RLS on meta.sessions for dashboard readers is configured at deploy time.
