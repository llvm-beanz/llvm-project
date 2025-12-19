namespace hlsl {
template <int, typename...> struct tuple_impl;

template <typename HEAD> struct tuple_impl<0, HEAD> {
  HEAD head;
};

template <int N, typename HEAD, typename... TAIL>
struct tuple_impl<N, HEAD, TAIL...> {
  HEAD head;
  tuple_impl<N - 1, TAIL...> tail;
};

template <typename... T> struct tuple {
  tuple_impl<sizeof...(T) - 1, T...> values;
};

uint StrToOffset(const char Str[]) {
  return (uint)Str; // Something to make this sane.
}

ByteAddressBuffer DebugOutput : register(u0, space9);
groupshared uint OutputOffset = 0;

struct MessagePrefix {
  unsigned LaneCount;
  unsigned Size;
};

template <typename... T> void printf(const char Str[], T... Args) {
  using ArgTuple_t = tuple<unsigned, unsigned, T...>;
  ArgTuple_t ArgStruct = {WaveGetLaneIndex(), StrToOffset(Str), Args...};
  int WaveOffset = WavePrefixSum(1) - 1;
  int WaveCount = WaveActiveSum(1);
  uint ThreadOffset =
      OutputOffset + sizeof(MessagePrefix) + (WaveOffset * sizeof(ArgTuple_t));
  if (WaveIsFirstLane()) {
    MessagePrefix Prefix = {WaveCount, sizeof(ArgTuple_t)};
    DebugOutput.Store<MessagePrefix>(OutputOffset, Prefix);
    OutputOffset += sizeof(MessagePrefix) + (WaveCount * sizeof(ArgTuple_t));
  }
  DebugOutput.Store<ArgTuple_t>(ArgStruct);
}

} // namespace hlsl
