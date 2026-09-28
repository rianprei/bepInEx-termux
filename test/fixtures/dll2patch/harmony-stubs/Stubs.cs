// Stubs mínimos de HarmonyX — só a assinatura dos atributos usados nos testes.
// Não é o HarmonyX real; não baixa nada. Só para o dotnet compilar a fixture.
using System;

namespace HarmonyLib
{
    [AttributeUsage(AttributeTargets.Class | AttributeTargets.Method, AllowMultiple = true)]
    public sealed class HarmonyPatch : Attribute
    {
        public Type type;
        public string method;
        public Type[] argumentTypes;

        public HarmonyPatch() { }
        public HarmonyPatch(Type type) { this.type = type; }
        public HarmonyPatch(Type type, string method) { this.type = type; this.method = method; }
        public HarmonyPatch(Type type, string method, Type[] argumentTypes)
        {
            this.type = type; this.method = method; this.argumentTypes = argumentTypes;
        }
    }

    [AttributeUsage(AttributeTargets.Method)]
    public sealed class HarmonyPrefix : Attribute { }

    [AttributeUsage(AttributeTargets.Method)]
    public sealed class HarmonyPostfix : Attribute { }

    [AttributeUsage(AttributeTargets.Method)]
    public sealed class HarmonyTranspiler : Attribute { }

    public enum HarmonyPriority
    {
        Last = 0,
        First = 100
    }

    [AttributeUsage(AttributeTargets.Method)]
    public sealed class HarmonyPriorityAttribute : Attribute
    {
        public int priority;
        public HarmonyPriorityAttribute(int priority) { this.priority = priority; }
    }
}
