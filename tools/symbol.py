#!/usr/bin/env python
# Copyright (c) 2012 Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Normalizes and de-duplicates paths within Breakpad symbol files.

When using DWARF for storing debug symbols, some file information will be
stored relative to the current working directory of the current compilation
unit, and may be further relativized based upon how the file was #included.

This helper can be used to parse the Breakpad symbol file generated from such
DWARF files and normalize and de-duplicate the FILE records found within,
updating any references to the FILE records in the other record types.
"""

import macpath
import ntpath
import optparse
import os
import posixpath
import sys

class BreakpadParseError(Exception):
  """Unsupported Breakpad symbol record exception class."""
  pass
  

class SymbolFileParser(object):
  """Parser for Breakpad symbol files.

  The format of these files is documented at
  https://chromium.googlesource.com/breakpad/breakpad/+/master/docs/symbol_files.md
  """

  def __init__(self, input_stream):
    """Inits a SymbolFileParser to read symbol records from |input_stream| and
    write the processed output to |output_stream|.
    
    |ignored_prefixes| contains a list of optional path prefixes that
    should be stripped from the final, normalized path outputs.
    
    For example, if the Breakpad symbol file had all paths starting with a
    common prefix, such as:
      FILE 1 /b/build/src/foo.cc
      FILE 2 /b/build/src/bar.cc
    Then adding "/b/build/src" as an ignored prefix would result in an output
    file that contained:
      FILE 1 foo.cc
      FILE 2 bar.cc
    
    Note that |ignored_prefixes| does not necessarily contain file system
    paths, as the contents of the DWARF DW_AT_comp_dir attribute is dependent
    upon the host system and compiler, and may contain additional information
    such as hostname or compiler version.
    """

    self.unique_files = {}
    self.duplicate_files = {}
    self.input_stream = input_stream
    self.line_info_array = [];
    self.func_info_array = [];

  def Process(self):
    """Processes the Breakpad symbol file."""
    for line in self.input_stream:
      self._ParseRecord(line.rstrip())
    #print(self.line_info_array)
    #print(self.func_info_array)
    #print(self.unique_files)

  def _ParseRecord(self, record):
    """Parses a single Breakpad symbol record - a single line from the symbol
    file.

    Returns:
        The modified string to write to the output file, or None if no line
        should be written.
    """
    record_type = record.partition(' ')[0]
    if record_type == 'FILE':
      return self._ParseFileRecord(record)
    elif self._IsLineRecord(record_type) or record_type == 'FUNC':
      return self._ParseLineRecord(record, record_type)
    else:
      # Simply pass the record through unaltered.
      return record


  def _ParseFileRecord(self, file_record):
    """Parses and corrects a FILE record."""
    file_info = file_record[5:].split(' ', 3)
    if len(file_info) > 2:
      raise BreakpadParseError('Unsupported FILE record: ' + file_record)
    file_index = int(file_info[0])
    file_name =  file_info[1]
    self.unique_files[file_index] = file_name

  def _IsLineRecord(self, record_type):
    """Determines if the current record type is a Line record"""
    try:
      line = int(record_type, 16)
    except (ValueError, TypeError):
      return False
    return True

  def _ParseLineRecord(self, line_record, record_type):
    """Parses and corrects a Line record."""
    if record_type == 'FUNC':
        line_info = line_record.split(' ', 4);
        self.func_info_array.append({'a':int(line_info[1], 16) ,'n':line_info[4]})
        return

    line_info = line_record.split(' ', 5)
    if len(line_info) > 4:
      raise BreakpadParseError('Unsupported Line record: ' + line_record)
    file_index = int(line_info[3])
	
    self.line_info_array.append({'f':file_index,'a':int(record_type, 16),'l':int(line_info[2])})
    
  def BinarySearch(self, array, t):
  
    if len(array) == 0:
        return -1
  
    low = 0
    height = len(array)-1
       
    if array[height]['a'] <= t:
        return height
    
    while low <= height:
        mid = (low+height)/2
        
        if array[mid]['a'] <= t and array[mid + 1]['a'] > t:
            return mid
        
        if array[mid]['a'] < t:
            low = mid + 1

        elif array[mid]['a'] > t:
            height = mid - 1
            
        else:
            return mid
            
    return -1
    
  def find_function(self, addr):
    index = self.BinarySearch(self.func_info_array, addr)
    #print(index);
    
    if index == -1:
        return None
        
    return self.func_info_array[index]['n']
  def find_file_line(self, addr):
    index = self.BinarySearch(self.line_info_array, addr)
    #print(index);
    if index == -1:
        return None
    
    obj = self.line_info_array[index]
    return {"line": obj['l'], 'file':self.unique_files[obj['f']]}
  
def main():
       
  try:
    f = open(sys.argv[1], 'r');
    outf = open (sys.argv[2],'w')

 

    elf_file_map = {}
    
    for line in f.readlines():
        if line.partition(' ')[0] == '##':
            line_info = line.split(' ', 3)
            file = line_info[1];
            if elf_file_map.get(file) is None:
                command  = './dump_syms ' + line_info[1] + ' >/tmp/heapdump.tmp.txt'
                print(command)
                os.system(command)
                
                tmpf = open('/tmp/heapdump.tmp.txt')
                symbol_parser = SymbolFileParser(tmpf.readlines())
                symbol_parser.Process();
                elf_file_map[file] = symbol_parser;
                tmpf.close();                
                
            symbol_parser = elf_file_map[file]
            
            addr = int(line_info[2])
            sym_obj = symbol_parser.find_file_line(addr)
            func_name = symbol_parser.find_function(addr)
            if sym_obj and func_name:
              tmpline = "%s %s at %s:%s\n" % (file, func_name, sym_obj['file'], str(sym_obj['line']))
            else:
              tmpline = line
            outf.write(tmpline)
        else:
            outf.write(line)
    outf.close();
    f.close();
  except BreakpadParseError, e:
    print >> sys.stderr, 'Got an error while processing symbol file'
    print >> sys.stderr, str(e)
    return 1
  return 0

if __name__ == '__main__':
  sys.exit(main())
