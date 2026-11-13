using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Mono.Cecil;
using Mono.Cecil.Cil;

internal static class PWAATManagedPatch
{
    private static IEnumerable<TypeDefinition> AllTypes(IEnumerable<TypeDefinition> types)
    {
        foreach (var type in types)
        {
            yield return type;
            foreach (var nested in AllTypes(type.NestedTypes))
                yield return nested;
        }
    }

    private static string Fingerprint(MethodDefinition method)
    {
        if (!method.HasBody)
            return "";
        var body = method.Body;
        return string.Join("|", body.Instructions.Select(i =>
        {
            var target = i.Operand as Instruction;
            var operand = target != null ? "b" + body.Instructions.IndexOf(target)
                : i.Operand == null ? "" : i.Operand.ToString();
            return i.OpCode.Code + ":" + operand;
        }).ToArray()) + ";" + string.Join(",", body.Variables.Select(v => v.VariableType.FullName).ToArray());
    }

    private static Dictionary<string, string> Fingerprints(ModuleDefinition module)
    {
        return AllTypes(module.Types).SelectMany(t => t.Methods).ToDictionary(m => m.FullName, Fingerprint);
    }

    private static bool IsCall(Instruction i, string type, string name)
    {
        var m = i.Operand as MethodReference;
        return (i.OpCode.Code == Code.Call || i.OpCode.Code == Code.Callvirt) && m != null
            && m.DeclaringType.FullName == type && m.Name == name;
    }

    private static bool IsField(Instruction i, Code code, string type, string name)
    {
        var f = i.Operand as FieldReference;
        return i.OpCode.Code == code && f != null && f.DeclaringType.FullName == type && f.Name == name;
    }

    private static bool IsInt(Instruction i, int value)
    {
        return i.OpCode.Code == Code.Ldc_I4_S && (sbyte)i.Operand == value
            || i.OpCode.Code == Code.Ldc_I4 && (int)i.Operand == value;
    }

    private static VariableDefinition Local(Instruction i, MethodBody body, bool store)
    {
        switch (i.OpCode.Code)
        {
            case Code.Stloc_0: return store ? body.Variables[0] : null;
            case Code.Stloc_1: return store ? body.Variables[1] : null;
            case Code.Stloc_2: return store ? body.Variables[2] : null;
            case Code.Stloc_3: return store ? body.Variables[3] : null;
            case Code.Ldloc_0: return store ? null : body.Variables[0];
            case Code.Ldloc_1: return store ? null : body.Variables[1];
            case Code.Ldloc_2: return store ? null : body.Variables[2];
            case Code.Ldloc_3: return store ? null : body.Variables[3];
            case Code.Stloc:
            case Code.Stloc_S:
                return store ? i.Operand as VariableDefinition : null;
            case Code.Ldloc:
            case Code.Ldloc_S:
                return store ? null : i.Operand as VariableDefinition;
        }
        return null;
    }

    private static int PatchRemainderChecks(MethodDefinition method)
    {
        var ins = method.Body.Instructions;
        int count = 0;
        for (int i = 0; i + 3 < ins.Count; i++)
        {
            bool h = IsCall(ins[i], "UnityEngine.Resolution", "get_height") && IsInt(ins[i + 1], 9);
            bool w = IsCall(ins[i], "UnityEngine.Resolution", "get_width") && IsInt(ins[i + 1], 16);
            if ((h || w) && ins[i + 2].OpCode.Code == Code.Rem && ins[i + 3].OpCode.Code == Code.Brtrue)
            {
                ins[i + 1].OpCode = OpCodes.Ldc_I4;
                ins[i + 1].Operand = 1;
                count++;
            }
        }
        return count;
    }

    private static int PatchDivisionChecks(MethodDefinition method)
    {
        var ins = method.Body.Instructions;
        var locals = new List<VariableDefinition>();
        for (int i = 0; i + 5 < ins.Count; i++)
            if (IsCall(ins[i], "UnityEngine.Resolution", "get_width") && IsInt(ins[i + 1], 16)
                && ins[i + 2].OpCode.Code == Code.Div && IsInt(ins[i + 3], 9)
                && ins[i + 4].OpCode.Code == Code.Mul && Local(ins[i + 5], method.Body, true) != null)
                locals.Add(Local(ins[i + 5], method.Body, true));
        int count = 0;
        var processor = method.Body.GetILProcessor();
        for (int i = 0; i + 2 < ins.Count; i++)
        {
            if (!IsCall(ins[i], "UnityEngine.Resolution", "get_height"))
                continue;
            var local = Local(ins[i + 1], method.Body, false);
            if (local == null || !locals.Contains(local) || ins[i + 2].OpCode.Code != Code.Bne_Un)
                continue;
            ins[i + 2].OpCode = OpCodes.Pop;
            ins[i + 2].Operand = null;
            processor.InsertAfter(ins[i + 2], Instruction.Create(OpCodes.Pop));
            count++;
        }
        if (count != locals.Count)
            throw new InvalidOperationException("Unmatched 16:9 division check in " + method.FullName);
        return count;
    }

    private static void InsertBefore(MethodDefinition method, Instruction target, Instruction[] added)
    {
        var processor = method.Body.GetILProcessor();
        foreach (var instruction in method.Body.Instructions)
        {
            if (instruction.Operand == target)
                instruction.Operand = added[0];
            var branches = instruction.Operand as Instruction[];
            if (branches != null)
                for (int i = 0; i < branches.Length; i++)
                    if (branches[i] == target)
                        branches[i] = added[0];
        }
        foreach (var handler in method.Body.ExceptionHandlers)
        {
            if (handler.TryStart == target)
                handler.TryStart = added[0];
            if (handler.TryEnd == target)
                handler.TryEnd = added[0];
            if (handler.HandlerStart == target)
                handler.HandlerStart = added[0];
            if (handler.HandlerEnd == target)
                handler.HandlerEnd = added[0];
            if (handler.FilterStart == target)
                handler.FilterStart = added[0];
        }
        foreach (var instruction in added)
            processor.InsertBefore(target, instruction);
    }

    private static int Main(string[] args)
    {
        if (args.Length != 3)
        {
            Console.Error.WriteLine("usage: PWAATManagedPatch <Assembly-CSharp.dll> <output> <PWAATResolution.dll>");
            return 2;
        }
        var resolver = new DefaultAssemblyResolver();
        resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(args[0])));
        var parameters = new ReaderParameters { AssemblyResolver = resolver };
        using (var original = ModuleDefinition.ReadModule(args[0], parameters))
        using (var module = ModuleDefinition.ReadModule(args[0], parameters))
        using (var helper = ModuleDefinition.ReadModule(args[2]))
        {
            if (module.AssemblyReferences.Any(r => r.Name == helper.Assembly.Name.Name))
                throw new InvalidOperationException("Assembly-CSharp.dll is already patched");
            var before = Fingerprints(original);
            var helperType = helper.GetType("PWAATResolution");
            var getResolutions = module.ImportReference(helperType.Methods.Single(m => m.Name == "GetResolutions"));
            var applyStartup = module.ImportReference(helperType.Methods.Single(m => m.Name == "ApplyStartup"));
            var hideFrame = module.ImportReference(helperType.Methods.Single(m => m.Name == "HideFrame"));
            var allowed = new HashSet<string>();
            int filters = 0, redirects = 0;

            foreach (var method in AllTypes(module.Types).SelectMany(t => t.Methods).Where(m => m.HasBody).ToArray())
            {
                int found = PatchRemainderChecks(method) + PatchDivisionChecks(method);
                foreach (var site in method.Body.Instructions.Where(i => IsCall(i, "UnityEngine.Screen", "get_resolutions")))
                {
                    site.Operand = getResolutions;
                    redirects++;
                    found++;
                }
                filters += found;
                if (found > 0)
                    allowed.Add(method.FullName);
            }
            if (filters == redirects || redirects == 0)
                throw new InvalidOperationException("Resolution filters not found");

            var startup = AllTypes(module.Types)
                .Where(t => t.DeclaringType != null && t.DeclaringType.Name == "startCtrl")
                .SelectMany(t => t.Methods).Where(m => m.HasBody && m.Name == "MoveNext")
                .SelectMany(m => m.Body.Instructions.Select((ins, index) => new { m, ins, index }))
                .Where(x =>
                {
                    var list = x.m.Body.Instructions;
                    return x.index + 5 < list.Count
                        && IsField(list[x.index], Code.Ldsfld, "GSStatic", "option_work")
                        && IsField(list[x.index + 1], Code.Ldfld, "OptionWork", "resolution_w")
                        && list[x.index + 2].OpCode.Code == Code.Brfalse
                        && IsField(list[x.index + 3], Code.Ldsfld, "GSStatic", "option_work")
                        && IsField(list[x.index + 4], Code.Ldfld, "OptionWork", "resolution_h")
                        && list[x.index + 5].OpCode.Code == Code.Brfalse;
                }).ToArray();
            if (startup.Length != 1)
                throw new InvalidOperationException("Startup resolution sites: " + startup.Length);
            var optionWork = (FieldReference)startup[0].ins.Operand;
            var startupList = startup[0].m.Body.Instructions;
            var widthField = (FieldReference)startupList[startup[0].index + 1].Operand;
            var heightField = (FieldReference)startupList[startup[0].index + 4].Operand;
            if (optionWork.FieldType.IsValueType)
                throw new InvalidOperationException("Unexpected OptionWork layout");
            InsertBefore(startup[0].m, startup[0].ins, new[] {
                Instruction.Create(OpCodes.Ldsfld, optionWork),
                Instruction.Create(OpCodes.Ldflda, widthField),
                Instruction.Create(OpCodes.Ldsfld, optionWork),
                Instruction.Create(OpCodes.Ldflda, heightField),
                Instruction.Create(OpCodes.Call, applyStartup)
            });
            allowed.Add(startup[0].m.FullName);

            var frameStart = module.GetType("FrameCtrl").Methods.Single(m => m.Name == "Start" && m.HasBody);
            frameStart.Body.Instructions.Clear();
            frameStart.Body.Variables.Clear();
            frameStart.Body.ExceptionHandlers.Clear();
            var processor = frameStart.Body.GetILProcessor();
            processor.Emit(OpCodes.Ldarg_0);
            processor.Emit(OpCodes.Call, hideFrame);
            processor.Emit(OpCodes.Ret);
            allowed.Add(frameStart.FullName);

            var after = Fingerprints(module);
            var changed = after.Where(kv => before[kv.Key] != kv.Value).Select(kv => kv.Key).ToArray();
            if (after.Count != before.Count || changed.Any(name => !allowed.Contains(name)))
                throw new InvalidOperationException("Unexpected changed methods: " + string.Join(", ", changed));
            foreach (var name in changed.OrderBy(n => n, StringComparer.Ordinal))
                Console.WriteLine("patched " + name);
            Console.WriteLine("filters " + (filters - redirects) + ", resolution lists " + redirects);
            module.Write(args[1]);
        }
        return 0;
    }
}
