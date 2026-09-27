using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

if (args.Length != 2)
    throw new ArgumentException("Uso: Facts <dll> <diretorio-de-saida>");

using var stream = File.OpenRead(args[0]);
using var pe = new PEReader(stream);
var headers = pe.PEHeaders;
var factsPath = Path.Combine(args[1], "pe_facts.tsv");
using (var facts = new StreamWriter(factsPath, false, new UTF8Encoding(false)))
{
    facts.WriteLine($"metadata\t{headers.MetadataStartOffset}\t{headers.CorHeader?.MetadataDirectory.Size ?? 0}");
    foreach (var section in headers.SectionHeaders)
    {
        facts.WriteLine(string.Join('\t', "section", section.Name, section.VirtualAddress,
            section.VirtualSize, section.SizeOfRawData, section.PointerToRawData));
    }
    MetadataReader metadata = pe.GetMetadataReader();
    foreach (TableIndex table in new[] {
        TableIndex.TypeDef, TableIndex.MethodDef, TableIndex.CustomAttribute,
        TableIndex.MemberRef, TableIndex.TypeRef, TableIndex.Field, TableIndex.Param
    })
    {
        facts.WriteLine($"rows\t{table}\t{metadata.GetTableRowCount(table)}");
    }
    for (int rid = 1; rid <= metadata.GetTableRowCount(TableIndex.MethodDef); rid++)
    {
        MethodDefinition method = metadata.GetMethodDefinition(MetadataTokens.MethodDefinitionHandle(rid));
        facts.WriteLine($"method\t{rid}\t{metadata.GetString(method.Name)}\t"
            + $"{method.RelativeVirtualAddress}\t{method.GetParameters().Count}");
    }
}

var opcodePath = Path.Combine(args[1], "opcodes_table.csv");
var opcodes = typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static)
    .Where(field => field.FieldType == typeof(OpCode))
    .Select(field => (OpCode)field.GetValue(null)!)
    .OrderBy(opcode => unchecked((ushort)opcode.Value));
using (var output = new StreamWriter(opcodePath, false, new UTF8Encoding(false)))
{
    output.WriteLine("Value,Name,OperandType,Size");
    foreach (var opcode in opcodes)
    {
        output.WriteLine($"0x{unchecked((ushort)opcode.Value):X4},{opcode.Name},{opcode.OperandType},{opcode.Size}");
    }
}
