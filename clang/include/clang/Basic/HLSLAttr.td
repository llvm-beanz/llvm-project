class HLSLSemantic<string name> : Spelling<name, "HLSLSemantic">;

def NumThreadsDocs : Documentation {
  let Category = DocCatFunction;
  let Content = [{
The ``numthreads`` attribute applies to HLSL shaders where explcit thread counts
are required. The ``X``, ``Y``, and ``Z`` values provided to the attribute
dictate the thread id. Total number of threads executed is ``X * Y * Z``.
  }];
}

def HLSLNumThreads: InheritableAttr {
  let Spellings = [Microsoft<"numthreads">];
  let Args = [IntArgument<"X">, IntArgument<"Y">, IntArgument<"Z">];
  let Subjects = SubjectList<[Function]>;
  let LangOpts = [HLSL];
  let Documentation = [NumThreadsDocs];
}

def HLSLSV_GroupIndexDocs : Documentation {
  let Category = DocCatFunction;
  let Content = [{
The `SV_GroupIndex` semantic when applied to an input parameter specifies a data
binding to map the group index to the specified parameter.
  }];
}

def HLSLSV_GroupIndex: InheritableAttr {
  let Spellings = [HLSLSemantic<"SV_GroupIndex">];
  let Subjects = SubjectList<[ParmVar, GlobalVar]>;
  let LangOpts = [HLSL];
  let Documentation = [HLSLSV_GroupIndexDocs];
}
