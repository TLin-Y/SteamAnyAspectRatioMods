using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using Mono.Cecil;
using Mono.Cecil.Cil;

internal static class AI1ManagedPatch
{
    private const string OriginalHash =
        "B539533BBA8DF35D1AD1FADFDFA8A362EC5061D9C6626DC1A2EF16DABE793F09";

    private sealed class Target
    {
        internal readonly string Type;
        internal readonly string Method;
        internal readonly int Offset;
        internal readonly Code CurrentAspect;
        internal readonly Code ReferenceAspect;

        internal Target(string type, string method, int offset,
            Code currentAspect, Code referenceAspect)
        {
            Type = type;
            Method = method;
            Offset = offset;
            CurrentAspect = currentAspect;
            ReferenceAspect = referenceAspect;
        }
    }

    private static readonly Target[] Targets =
    {
        new Target("FixAspectRatio", "Start", 0x14, Code.Ldloc_1, Code.Ldloc_0),
        new Target("Game.CameraScaler", "Update", 0x3c, Code.Ldloc_2, Code.Ldloc_1),
        new Target("Game.ScreenScaler", "Update", 0x14, Code.Ldloc_1, Code.Ldloc_0),
        new Target("Game.ScreenScalerSafeArea", "Update", 0x14, Code.Ldloc_1, Code.Ldloc_0)
    };

    private static string Hash(string path)
    {
        using (var sha = SHA256.Create())
        using (var stream = File.OpenRead(path))
            return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "");
    }

    private static IEnumerable<TypeDefinition> AllTypes(IEnumerable<TypeDefinition> types)
    {
        foreach (var type in types)
        {
            yield return type;
            foreach (var nested in AllTypes(type.NestedTypes))
                yield return nested;
        }
    }

    private static string Operand(Instruction instruction, MethodBody body)
    {
        var branch = instruction.Operand as Instruction;
        if (branch != null)
            return "branch:" + body.Instructions.IndexOf(branch);
        var branches = instruction.Operand as Instruction[];
        if (branches != null)
            return string.Join(",", branches.Select(b => body.Instructions.IndexOf(b)));
        return instruction.Operand == null ? "" : instruction.Operand.ToString();
    }

    private static string Fingerprint(MethodDefinition method)
    {
        if (!method.HasBody)
            return "";
        var body = method.Body;
        return string.Join("|", body.Instructions.Select(i => i.OpCode.Code + ":" + Operand(i, body)))
            + ";locals:" + string.Join(",", body.Variables.Select(v => v.VariableType.FullName))
            + ";handlers:" + string.Join(",", body.ExceptionHandlers.Select(h =>
                h.HandlerType + ":" + body.Instructions.IndexOf(h.TryStart) + ":"
                + body.Instructions.IndexOf(h.TryEnd) + ":" + body.Instructions.IndexOf(h.HandlerStart)
                + ":" + body.Instructions.IndexOf(h.HandlerEnd) + ":"
                + body.Instructions.IndexOf(h.FilterStart) + ":" + h.CatchType));
    }

    private static Dictionary<string, string> Fingerprints(ModuleDefinition module)
    {
        return AllTypes(module.Types).SelectMany(t => t.Methods)
            .ToDictionary(m => m.FullName, Fingerprint);
    }

    private static void BeforeReturns(MethodDefinition method, Func<Instruction[]> create)
    {
        var processor = method.Body.GetILProcessor();
        foreach (var ret in method.Body.Instructions.Where(i => i.OpCode.Code == Code.Ret).ToArray())
        {
            var added = create();
            foreach (var instruction in method.Body.Instructions.ToArray())
            {
                if (instruction.Operand == ret)
                    instruction.Operand = added[0];
                var branches = instruction.Operand as Instruction[];
                if (branches != null)
                    for (int i = 0; i < branches.Length; i++)
                        if (branches[i] == ret)
                            branches[i] = added[0];
            }
            foreach (var instruction in added)
                processor.InsertBefore(ret, instruction);
        }
    }

    private static int Main(string[] args)
    {
        if (args.Length != 3)
        {
            Console.Error.WriteLine("Usage: AI1ManagedPatch.exe ORIGINAL_DLL OUTPUT_DLL AI1Resolution.dll");
            return 2;
        }
        try
        {
            var input = Path.GetFullPath(args[0]);
            var output = Path.GetFullPath(args[1]);
            if (string.Equals(input, output, StringComparison.OrdinalIgnoreCase) || File.Exists(output))
                throw new InvalidOperationException("Output must be a new file, not the installed DLL.");
            if (Hash(input) != OriginalHash)
                throw new InvalidOperationException("Unsupported or already modified Assembly-CSharp.dll.");

            Dictionary<string, string> expected;
            byte[] patched;
            var changed = new HashSet<string>();
            using (var resolver = new DefaultAssemblyResolver())
            {
                resolver.AddSearchDirectory(Path.GetDirectoryName(input));
                using (var module = ModuleDefinition.ReadModule(input,
                    new ReaderParameters { AssemblyResolver = resolver }))
                {
                    var before = Fingerprints(module);
                    foreach (var target in Targets)
                    {
                        var type = module.GetType(target.Type);
                        if (type == null)
                            throw new InvalidOperationException("Missing type: " + target.Type);
                        var method = type.Methods.Single(m => m.Name == target.Method);
                        var instructions = method.Body.Instructions;
                        var first = instructions.Single(i => i.Offset == target.Offset);
                        var second = first.Next;
                        var third = second == null ? null : second.Next;
                        if (first.OpCode.Code != target.CurrentAspect || second == null
                            || second.OpCode.Code != target.ReferenceAspect || third == null
                            || third.OpCode.Code != Code.Div)
                            throw new InvalidOperationException("Unexpected aspect calculation: " + method.FullName);

                        first.OpCode = OpCodes.Ldc_R4;
                        first.Operand = 1f;
                        second.OpCode = OpCodes.Nop;
                        second.Operand = null;
                        third.OpCode = OpCodes.Nop;
                        third.Operand = null;
                        changed.Add(method.FullName);
                        Console.WriteLine("Patched " + method.FullName);
                    }
                    var launcher = module.GetType("Game.LauncherArgs").Methods
                        .Single(m => m.Name == "OnRuntimeMethodLoad");
                    var resolutionCall = launcher.Body.Instructions.Single(i =>
                        i.OpCode.Code == Code.Call && i.Operand != null
                        && i.Operand.ToString() ==
                        "System.Void UnityEngine.Screen::SetResolution(System.Int32,System.Int32,System.Boolean)");
                    var resolutionStart = launcher.Body.Instructions.Single(i => i.Offset == 0x247);
                    if (resolutionStart.OpCode.Code != Code.Call
                        || resolutionStart.Operand.ToString() !=
                        "System.Int32 Game.LauncherArgs::get_ResolutionW()"
                        || resolutionCall.Offset != 0x259)
                        throw new InvalidOperationException("Unexpected startup resolution sequence.");
                    using (var helper = ModuleDefinition.ReadModule(Path.GetFullPath(args[2])))
                    {
                        if (helper.Assembly.Name.Name != "AI1Resolution"
                            || helper.RuntimeVersion != module.RuntimeVersion)
                            throw new InvalidOperationException("Unexpected resolution helper identity/runtime.");
                        var replacement = helper.GetType("AI1Resolution").Methods.Single(m =>
                            m.Name == "Configure" && m.IsPublic && m.IsStatic
                            && m.ReturnType.FullName == "System.Void"
                            && string.Join(",", m.Parameters.Select(p => p.ParameterType.FullName)) ==
                            "System.Int32&,System.Int32&,System.Boolean&");
                        var processor = launcher.Body.GetILProcessor();
                        foreach (var name in new[] { "m_ResolutionW", "m_ResolutionH", "m_IsWindowed" })
                            processor.InsertBefore(resolutionStart, Instruction.Create(OpCodes.Ldsflda,
                                launcher.DeclaringType.Fields.Single(f => f.Name == name)));
                        processor.InsertBefore(resolutionStart,
                            Instruction.Create(OpCodes.Call, module.ImportReference(replacement)));
                        var helperType = helper.GetType("AI1Resolution");
                        var layout = module.ImportReference(helperType.Methods.Single(m =>
                            m.Name == "AdjustLayout"));
                        var scaler = module.GetType("Game.ScreenScaler");
                        var update = scaler.Methods.Single(m => m.Name == "Update");
                        BeforeReturns(update, () => new[] {
                            Instruction.Create(OpCodes.Ldarg_0),
                            Instruction.Create(OpCodes.Ldfld, scaler.Fields.Single(f =>
                                f.Name == "rectTransform")),
                            Instruction.Create(OpCodes.Call, layout)
                        });
                        var controller = module.GetType("Game.LuaCameraController");
                        var camera = controller.Fields.Single(f => f.Name == "camera_");
                        var renderType = controller.Fields.Single(f => f.Name == "renderTextureType");
                        var apply = module.ImportReference(helperType.Methods.Single(m =>
                            m.Name == "ApplyProjection"));
                        var restore = module.ImportReference(helperType.Methods.Single(m =>
                            m.Name == "RestoreProjection"));
                        var preCull = controller.Methods.Single(m => m.Name == "OnPreCull");
                        BeforeReturns(preCull, () => new[] {
                            Instruction.Create(OpCodes.Ldarg_0),
                            Instruction.Create(OpCodes.Ldfld, camera),
                            Instruction.Create(OpCodes.Ldarg_0),
                            Instruction.Create(OpCodes.Ldfld, renderType),
                            Instruction.Create(OpCodes.Call, apply)
                        });
                        changed.Add(preCull.FullName);
                        foreach (var name in new[] { "OnPostRender", "OnDisable" })
                        {
                            var method = controller.Methods.Single(m => m.Name == name);
                            var start = method.Body.Instructions[0];
                            var il = method.Body.GetILProcessor();
                            il.InsertBefore(start, Instruction.Create(OpCodes.Ldarg_0));
                            il.InsertBefore(start, Instruction.Create(OpCodes.Ldfld, camera));
                            il.InsertBefore(start, Instruction.Create(OpCodes.Call, restore));
                            changed.Add(method.FullName);
                        }
                    }
                    changed.Add(launcher.FullName);
                    Console.WriteLine("Added INI configuration before the original launcher resolution call.");
                    expected = Fingerprints(module);
                    var actualChanges = before.Keys.Where(k => before[k] != expected[k]).ToArray();
                    if (actualChanges.Length != Targets.Length + 4 || actualChanges.Any(k => !changed.Contains(k)))
                        throw new InvalidOperationException("Unexpected method changes.");
                    using (var buffer = new MemoryStream())
                    {
                        module.Write(buffer);
                        patched = buffer.ToArray();
                    }
                }
            }

            using (var buffer = new MemoryStream(patched, false))
            using (var written = ModuleDefinition.ReadModule(buffer))
            {
                var actual = Fingerprints(written);
                if (actual.Count != expected.Count || expected.Any(k =>
                    !actual.ContainsKey(k.Key) || actual[k.Key] != k.Value))
                    throw new InvalidOperationException("Written assembly failed method-body verification.");
            }
            using (var file = new FileStream(output, FileMode.CreateNew, FileAccess.Write))
                file.Write(patched, 0, patched.Length);
            Console.WriteLine("Verified: eight camera/layout/startup methods changed; other method bodies preserved.");
            Console.WriteLine("Original SHA256: " + OriginalHash);
            Console.WriteLine("Patched  SHA256: " + Hash(output));
            Console.WriteLine("AI: THE SOMNIUM FILES any-aspect-ratio patch created.");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.ToString());
            return 1;
        }
    }
}
