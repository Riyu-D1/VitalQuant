-- 0002: clean layer + contact on_skin state
-- RAW stays immutable; CLEAN is resampled/interpolated views over raw windows
-- (docs/04, docs/06 S3). Everything derived carries pipeline_version + data_class.

create table if not exists clean.signal_windows (
  session_id       uuid not null,
  channel_id       text not null,
  window_start     timestamptz not null,
  resample_hz      real not null,
  n_samples        int not null,
  samples          real[] not null,        -- resampled to uniform grid
  gap_mask         smallint[] not null,    -- 0=real, 1=interpolated, 2=unfilled
  pipeline_version text not null,
  data_class       data_class not null,
  primary key (session_id, channel_id, window_start, resample_hz, pipeline_version)
);

-- temporal contact state (hysteresis) beside the per-window scalar quality
alter table quality.contact_state
  add column if not exists on_skin boolean;
