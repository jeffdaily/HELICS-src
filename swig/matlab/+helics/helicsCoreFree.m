function varargout = helicsCoreFree(varargin)
    %release the memory associated with a core
    %
  [varargout{1:nargout}] = helicsMEX(40, varargin{:});
end
