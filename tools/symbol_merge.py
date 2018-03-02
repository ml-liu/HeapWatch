# encoding = utf-8
import os
import re
import sys
from optparse import OptionParser

class HeapwatchMethod:
    BySize  = 1
    ByRefCount = 2
    ByAllocCount = 4
    ByFreeCount = 8


def merge(dump_file, code_dirs=[], exclude_dirs=[], strip_path=''):
    count_map = {}
    begin_count = False
    is_in = False
    count = 0
    method = 0
    regex = ".*Begin StackStorage stats, by method = (\d+).*"

    with open(dump_file, "r") as fp:
        newdumplines = fp.readlines()

    for line in newdumplines:
        if line.startswith(" *** Begin StackStorage stats"):
            result = re.match(regex, line)
            if result:
                method = int(result.group(1))
            else:
                return
            begin_count = True
        elif line.startswith(" *** End StackStorage stats"):
            begin_count = False
            count = 0
        elif line == "\n":
            is_in = False
            count = 0
        elif line.startswith("ref count: "):
            is_in = True
            result = re.match("ref count: (\d+), total alloc count: (\d+), total free count: (\d+), unfree size (\d+),", line)
            if result:
                if method == HeapwatchMethod.ByRefCount:
                  index = 1
                elif method == HeapwatchMethod.ByAllocCount:
                  index = 2
                elif method == HeapwatchMethod.ByFreeCount:
                  index = 3
                elif method == HeapwatchMethod.BySize:
                  index = 4
                else:
                  return
                count = int(result.group(index))
            else:
                print "analysis error..."
                return
        elif begin_count and is_in and is_in:
            is_exclude_dir = False
            if code_dirs:
                is_exclude_dir = True
                for d in code_dirs:
                    if d in line:
                        is_exclude_dir = False
                        break
                if is_exclude_dir:
                    continue
            for d in exclude_dirs:
                if d in line:
                  is_exclude_dir = True
                  break
            if is_exclude_dir:
		print "2222"
                continue
            if strip_path:
                format_line = line.replace(strip_path, '')
            else:
                format_line = line
            if format_line in count_map:
                count_map[format_line] = count_map[format_line] + count
            else:
                count_map[format_line] = count
            is_in = False
              
    with open(dump_file+".merge" , "w+") as fp:
        for item in sorted(count_map.items(), key=lambda item:item[1], reverse=True):
            fp.write("%-8d%s" % (item[1], item[0]))
	    total_count += item[1]

def parse_args():
    usage = "usage : %prog [option]"
    parse = OptionParser(usage = usage, version = "%prog v1.0")
    parse.add_option("--dumpfile", action="store", dest="dumpfile", default="", help="set dump file path")
    parse.add_option("--codedir", action="store", dest="codedir", default="", help="set project code dirs,split by,")
    parse.add_option("--exclude", action="store", dest="exclude", default="", help="set exclude dirs,split by,")
    parse.add_option("--strippath", action="store", dest="stirp", default="", help="set path strip")
    (options, args) = parse.parse_args()
    return (options, args)
    
if __name__=="__main__":
    (options, args) = parse_args()
    print options
    merge(dump_file=options.dumpfile,
          code_dirs=[] if options.codedir=='' else options.codedir.split(',') ,
          exclude_dirs=[] if options.exclude=='' else options.exclude.split(','),
          strip_path=options.stirp)
