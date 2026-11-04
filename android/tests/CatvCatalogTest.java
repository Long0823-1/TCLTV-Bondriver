package io.github.long0823.tclbondriver;
public final class CatvCatalogTest {
 static int checks;
 static void check(boolean value){checks++;if(!value)throw new AssertionError("check "+checks);}
 static void invalid(String text)throws Exception{boolean rejected=false;try{CatvCatalog.parse(text);}catch(java.io.IOException expected){rejected=true;}check(rejected);}
 public static void main(String[] args)throws Exception{
  String raw="4 16528 211 345000 1 1 arib:0e425331311b7ca4ecd6f3\n";
  check(CatvCatalog.parse(raw).get(0).name.equals("BS11イレブン"));
  check(CatvCatalog.normalize(raw).equals("4 16528 211 345000 1 1 BS11イレブン\n"));
  check(CatvCatalog.rows(raw).contains("type=TYPE_ISDB_C"));
  check(CatvCatalog.rows(raw).contains("service_id=211"));
  check(CatvCatalog.parse("# no cable channels\n").isEmpty());
  invalid(raw+raw);invalid("4 16528 211 89999 1 1 Bad\n");invalid("4 16528 211 345000 1 16 Bad\n");
  invalid("4 16528 211 345000 3 1 Bad\n");invalid("4 16528 211 345000 1 1 arib:bad\n");invalid("4294967300 16528 211 345000 1 1 Bad\n");
  System.out.println("CATV catalog checks: "+checks);
 }
}
