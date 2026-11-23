// For an RTTI type name (script arg, e.g. .?AVUINameBalloonText@@) find its vftable via the complete object locator
// and print the virtual function addresses.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.Function;

public class OgVtable extends GhidraScript {
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        for (String name : getScriptArgs()) {
            byte[] pat = name.getBytes("ASCII");
            Address hit = mem.findBytes(currentProgram.getMinAddress(), pat, null, true, monitor);
            if (hit == null) { println("NOTFOUND " + name); continue; }
            Address td = hit.subtract(8); // TypeDescriptor: vfptr, spare, name
            println("TD " + name + " @ " + td);
            // find COL: signature(0) offset cdOffset pTypeDescriptor pClassHierarchy
            byte[] tdb = new byte[] { (byte)td.getOffset(), (byte)(td.getOffset()>>8), (byte)(td.getOffset()>>16), (byte)(td.getOffset()>>24) };
            Address a = currentProgram.getMinAddress();
            while ((a = mem.findBytes(a, tdb, null, true, monitor)) != null) {
                Address col = a.subtract(12);
                if (mem.getInt(col) == 0 && mem.getInt(col.add(4)) == 0) {
                    byte[] colb = new byte[] { (byte)col.getOffset(), (byte)(col.getOffset()>>8), (byte)(col.getOffset()>>16), (byte)(col.getOffset()>>24) };
                    Address v = mem.findBytes(currentProgram.getMinAddress(), colb, null, true, monitor);
                    while (v != null) {
                        Address vt = v.add(4);
                        StringBuilder sb = new StringBuilder();
                        for (int i = 0; i < 40; i++) {
                            long f = mem.getInt(vt.add(i * 4)) & 0xffffffffL;
                            if (f < 0x401000L || f > 0xd00000L) break;
                            sb.append(String.format("[%d]%08x ", i, f));
                        }
                        println("VTABLE " + name + " @ " + vt + " : " + sb);
                        v = mem.findBytes(v.add(1), colb, null, true, monitor);
                    }
                }
                a = a.add(1);
            }
        }
    }
}
