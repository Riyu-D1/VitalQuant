# 04 — Database Schema (Step 4)

Target: **Supabase Postgres (15+/17)**. Key choices:

- **No TimescaleDB** — deprecated on PG17 Supabase. Declarative `PARTITION BY RANGE`
  on `sample_time` (monthly partitions; weekly if volume demands), `pg_partman` optional.
- **Layer separation as schema separation**: `raw`, `clean`, `quality`, `features`,
  `ml`, `meta`, `config`. Raw is append-only (role lacks UPDATE/DELETE).
- **Two storage shapes**: scalars row-per-sample; high-rate signals as **windowed arrays**
  (`signal_windows`) — ~100× write reduction vs row-per-sample, and waveforms stay raw.
- **`data_class` (`real|synthetic|simulated`) is `NOT NULL` everywhere** — synthetic and
  simulated data can never silently mix with real measurements (spec §24/§27).
- Units are fixed per channel in `config.channels` (SI only); no per-row unit strings.

```sql
create schema config;  create schema raw;    create schema clean;
create schema quality; create schema features; create schema ml; create schema meta;

create type data_class as enum ('real', 'synthetic', 'simulated');

-- ───────────────────────────── config / meta ─────────────────────────────

create table config.hardware_revisions (
  revision        text primary key,              -- 'hw_v0', 'hw_v1' ...
  description     text,
  profile         jsonb not null,                -- full resolved hardware profile
  created_at      timestamptz default now()
);

create table config.channels (                    -- unit + semantics registry
  channel_id   text primary key,                -- 'ppg.red','ppg.ir','spectral.f1',
                                                -- 'temp.object','env.rh','motion.ax','contact.level'
  sensor_type  text not null,                   -- 'optical','spectral','temperature','environment','motion','contact'
  unit         text,                            -- 'count','nm-normalised','degC','hPa','g','ohm' (gas only), null=unitless
  sample_role  text not null                    -- 'waveform','scalar','frame'
);

create table meta.devices (
  device_id         uuid primary key default gen_random_uuid(),
  label             text,
  hardware_revision text references config.hardware_revisions,
  api_key_hash      text not null,              -- sha256 of device key; raw key never stored
  created_at        timestamptz default now(),
  revoked           boolean default false
);

create table meta.subjects (                      -- pseudonymised people (audit G1/G13)
  subject_id   uuid primary key default gen_random_uuid(),
  external_ref text,                            -- study code, never name/email
  consent_ref  text,                            -- pointer to stored consent record
  created_at   timestamptz default now()
);

create table meta.sessions (
  session_id       uuid primary key default gen_random_uuid(),
  device_id        uuid references meta.devices not null,
  subject_id       uuid references meta.subjects,          -- null = bench/rig run
  firmware_version text not null,
  hardware_revision text references config.hardware_revisions not null,
  data_class       data_class not null default 'real',
  body_site        text,                                   -- 'wrist','finger','rig' (audit G7)
  started_at       timestamptz not null,
  ended_at         timestamptz,
  notes            text
);

-- ───────────────────────────── raw ─────────────────────────────

-- Exactly-once anchor: device assigns batch_id; retransmission after network failure
-- hits the unique constraint instead of duplicating rows.
create table raw.ingest_batches (
  batch_id         uuid not null,
  device_id        uuid references meta.devices not null,
  session_id       uuid references meta.sessions not null,
  received_at      timestamptz default now(),
  clock_offset_us  bigint,                       -- fitted device→UTC offset at batch time
  clock_drift_ppm  double precision,             -- fitted drift estimate
  payload_bytes    int,
  primary key (device_id, batch_id)
);

-- Scalar channels: temperature, environment, contact, spectral frame summaries, motion stats
create table raw.measurements_scalar (
  device_id    uuid not null,
  session_id   uuid not null,
  channel_id   text references config.channels not null,
  sample_time  timestamptz not null,             -- corrected UTC
  device_time_us bigint not null,                -- raw esp_timer value (clock model input)
  value        double precision,
  quality_flag smallint default 0,
  data_class   data_class not null,
  primary key (session_id, channel_id, sample_time)
) partition by range (sample_time);
-- partitions created per month: raw.measurements_scalar_2026_09 ...

-- High-rate waveforms: one row per (channel × window) with the waveform inline.
-- 200 Hz × 10 s PPG window = 2000 samples in one row vs 2000 rows.
create table raw.signal_windows (
  window_id        bigint generated always as identity,
  device_id        uuid not null,
  session_id       uuid not null,
  channel_id       text references config.channels not null,
  window_start     timestamptz not null,
  sample_rate_hz   real not null,
  n_samples        int not null,
  device_time_start_us bigint not null,
  samples          real[] not null,              -- or bytea w/ int16 PCM when storage demands
  quality_flag     smallint default 0,
  data_class       data_class not null,
  primary key (session_id, channel_id, window_start)
) partition by range (window_start);

create index on raw.signal_windows (session_id, channel_id, window_start);

-- Spectral: one row per frame keeps per-channel resolution + acquisition context.
create table raw.spectral_frames (
  frame_id      bigint generated always as identity,
  device_id     uuid not null,
  session_id    uuid not null,
  sample_time   timestamptz not null,
  device_time_us bigint not null,
  read_group    smallint not null,               -- AS7341 mux group → honesty about simultaneity (C2)
  channels      jsonb not null,                  -- {f1:…,f2:…,…,clear:…,nir:…,flicker:…}
  gain_x        real,
  integ_time_ms real,
  illumination  text,                            -- 'ambient','emitter_on','dark_frame'
  sensor_temp_c real,
  quality_flag  smallint default 0,
  data_class    data_class not null,
  primary key (session_id, sample_time, read_group)
) partition by range (sample_time);

-- Device-reported events: ntp sync, sensor failure, buffer overflow, reboot
create table raw.device_events (
  device_id  uuid not null,
  session_id uuid,
  event_time timestamptz not null,
  kind       text not null,                      -- 'ntp_sync','sensor_fault','buffer_flush',...
  detail     jsonb,
  primary key (device_id, event_time, kind)
);

-- ───────────────────────────── quality ─────────────────────────────

-- Per-window, per-channel quality — a first-class signal, not an annotation (spec §19).
create table quality.channel_quality (
  session_id   uuid not null,
  channel_id   text not null,
  window_start timestamptz not null,
  sqi          real not null check (sqi between 0 and 1),
  sqi_detail   jsonb,                            -- {skew:…,kurt:…,pi:…,motion_frac:…,template_r:…}
  pipeline_version text not null,
  primary key (session_id, channel_id, window_start, pipeline_version)
);

create table quality.contact_state (             -- fused contact/motion gate
  session_id   uuid not null,
  window_start timestamptz not null,
  contact_quality real check (contact_quality between 0 and 1),
  motion_score    real,
  sensor_confidence real,                        -- combined gate feeding fusion (spec §19)
  pipeline_version text not null,
  primary key (session_id, window_start, pipeline_version)
);

-- ───────────────────────────── features ─────────────────────────────

create table features.windows (                  -- windowed feature vectors
  session_id       uuid not null,
  window_start     timestamptz not null,
  window_seconds   real not null,
  feature_set      text not null,                -- 'ppg_v1','spectral_v1','fusion_v1'
  pipeline_version text not null,
  values           jsonb not null,               -- {hr_bpm: 62.1, rmssd_ms: …, r_ratio: …}
  source_window_ids bigint[],                    -- provenance → raw.signal_windows
  data_class       data_class not null,
  primary key (session_id, window_start, feature_set, pipeline_version)
);

-- Personal baselines (spec §23): robust per-subject/channel stats over trailing windows
create table features.baselines (
  subject_id   uuid references meta.subjects not null,
  channel_id   text not null,
  stat         text not null,                    -- 'median','mad','circadian_model_v1'
  window_days  int not null,                     -- trailing window used
  value        jsonb not null,
  computed_at  timestamptz default now(),
  pipeline_version text not null,
  primary key (subject_id, channel_id, stat, window_days, computed_at)
);

-- Optional honest labels (audit G9) — provenance required, never fabricated
create table meta.labels (
  session_id uuid references meta.sessions not null,
  label_time timestamptz not null,
  kind       text not null,                      -- 'self_report.illness','reference.temp',...
  value      jsonb not null,
  provenance text not null,                      -- 'self_report'|'reference_device'|'clinical'
  primary key (session_id, label_time, kind)
);

-- ───────────────────────────── ml ─────────────────────────────

create table ml.experiments (
  experiment_id  uuid primary key default gen_random_uuid(),
  name           text not null,
  dataset_version text not null,
  feature_set    text not null,
  model_spec     jsonb not null,                 -- {class:'logreg', params:{…}, seed:…}
  code_commit    text not null,                  -- spec §32
  created_at     timestamptz default now()
);

create table ml.models (
  model_id       uuid primary key default gen_random_uuid(),
  experiment_id  uuid references ml.experiments,
  artifact_uri   text,                           -- supabase storage / local path
  metrics        jsonb,                          -- {auroc:…, auprc:…, calibration:…}
  train_window   tstzrange,
  created_at     timestamptz default now()
);

create table ml.predictions (
  session_id    uuid not null,
  window_start  timestamptz not null,
  model_id      uuid references ml.models not null,
  kind          text not null,                   -- 'anomaly_score','feature_estimate',NEVER 'diagnosis'
  value         real,
  uncertainty   real,
  detail        jsonb,
  primary key (session_id, window_start, model_id, kind)
);
```

## Row-Level Security (spec §35)

```sql
alter table meta.sessions enable row level security;
-- dashboard users see only sessions for subjects they are linked to
-- (subject_access join table or JWT claim 'subject_ids'); devices write only
-- their own rows via a single Postgres role whose api_key_hash matches meta.devices.
```

## Sizing & retention

- Scalar ≈ 10–20 rows/s/device worst case — trivial.
- `signal_windows` at 200 Hz, 10 s windows, 3 ch: ~18 rows/min — ~26 k rows/day,
  ~70 MB/day float32; int16 PCM halves it. Monthly partitions; export aged `real`
  partitions to Storage as parquet before dropping (retention policy per `data_class`).
- `synthetic`/`simulated` classes get shorter default retention — they are regenerable.

## Integrity rules worth stating

- `raw.*` immutable: ingest role gets `INSERT/SELECT` only.
- `clean.*`/`features.*` rows always cite `pipeline_version` + source ids — reprocessing
  creates new versions rather than mutating (spec §11 "never overwrite raw").
- `data_class` flows device-side into every batch; backend rejects rows missing it.
