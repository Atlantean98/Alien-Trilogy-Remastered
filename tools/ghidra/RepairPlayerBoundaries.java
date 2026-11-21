//@category AlienTrilogy
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.io.*;

public class RepairPlayerBoundaries extends GhidraScript {
    public void run() throws Exception {
        Path reports=Paths.get(getScriptArgs()[0]);
        try(BufferedWriter writer=Files.newBufferedWriter(reports.resolve("player_boundary_repairs.tsv"))) {
            writer.write("entry\tinternal_start\tstatus\n");
            for(long entry:new long[]{0x3d930,0x3c94c,0x3c250,0x48bac,0x48e40,0x4bc88,0x4cfe8}) {
                int prefix=entry==0x4cfe8?2:5;
                Function outer=getFunctionAt(toAddr(entry));
                Function inner=getFunctionAt(toAddr(entry+prefix));
                if(inner==null){if(outer==null)throw new IllegalStateException("Missing true entry");writer.write(String.format("%08x\t%08x\talready_merged\n",entry,entry+prefix));continue;}
                // Verified bytes: PUSH EBX,ECX,EDX,ESI,EDI; PUSH EBP follows.
                byte[] expected={(byte)0x53,(byte)0x51,(byte)0x52,(byte)0x56,(byte)0x57,(byte)0x55};
                if(prefix==2)expected=new byte[]{(byte)0x56,(byte)0x57,(byte)0x55};
                byte[] bytes=getBytes(toAddr(entry),expected.length);
                if(!java.util.Arrays.equals(bytes,expected))throw new IllegalStateException("Prologue differs");
                if(outer==null){
                    disassemble(toAddr(entry));
                    outer=currentProgram.getFunctionManager().createFunction("RecoveredEntry_"+Long.toHexString(entry),toAddr(entry),new AddressSet(toAddr(entry),toAddr(entry+prefix-1)),SourceType.USER_DEFINED);
                }
                AddressSet body=new AddressSet(outer.getBody());body.add(inner.getBody());
                removeFunction(inner);outer.setBody(body);
                setPlateComment(toAddr(entry),"Verified full entry includes preserved register pushes before PUSH EBP. Internal analysis-created function boundary removed; not a jump wrapper.");
                writer.write(String.format("%08x\t%08x\tmerged\n",entry,entry+prefix));
            }
            for(long entry:new long[]{0x48a3c,0x4bc38,0x4bc6c,0x5b428,0x4abc0,0x5a186,0x5a567,0x5a991,0x5ad18,0x5b0f7})if(getFunctionAt(toAddr(entry))==null){
                disassemble(toAddr(entry));if(createFunction(toAddr(entry),"RecoveredPacket_"+Long.toHexString(entry))==null)throw new IllegalStateException("Cannot create packet callback");
            }
        }
    }
}
