function v = helics_pending_iterative_time_state()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = helicsMEX(0, 26);
  end
  v = vInitialized;
end
