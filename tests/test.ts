import { clib, Python } from '../src/python'
import assert = require('assert')

async function sleep (ms): Promise<void> {
  return await new Promise(resolve => setTimeout(resolve, ms))
}

function testDummy (): void {
  const py = new Python()
  py.add_syspath('plugins')
  const Dummy = py.import('Dummy')
  const dm = Dummy.Dummy().unwrap()
  assert(dm.dummy() === 'dummy')
  console.log('. testDummy OK!')
}

function testExecEval (): void {
  const py = new Python()
  py.exec(`def f(a, b):
       return a + b
    `)
  assert(py.eval('f(1, 2)') === 3)
  console.log('. testExecEval OK!')
}

function testExcel (): void {
  const py = new Python()
  py.add_syspath('plugins')
  let Excel
  // openpyxl可能没装, 所以如果出错这个就不测了
  try {
    Excel = py.import('Excel')
  } catch {
    return
  }

  const xls = Excel.Excel().unwrap()
  xls.open_workbook('tests/test.xlsx')
  const cell = 'A'
  xls.write_cell(0, 1, cell)
  const cell2 = xls.read_cell(0, 1)
  assert(cell === cell2)
  console.log('. testExcel OK!')
}

async function testAsync (): Promise<void> {
  const py = new Python()
  py.add_syspath('plugins')
  const Dummy = py.import('Dummy')
  const dm = Dummy.Dummy().unwrap()
  assert(await dm.dummy_async() === 'dummy')
  assert(await py.eval_async('1+2') === 3)
  py.eval_async('1+2').then(() => {
  }).catch(console.error)
  py.eval_async('1+2').then(() => {
  }).catch(console.error)
  py.eval_async('1+2').then(() => {
  }).catch(console.error)
  await py.exec_async('import time; time.sleep(0.001)')
  console.log('. testAsync OK!')
}

function testThreader (): void {
  const py = new Python()
  py.add_syspath('plugins')
  const Threader = py.import('Threader')
  const times = 100
  for (let i = 0; i < times; i++) {
    Threader.incr(1)
    Threader.incr_in_thread(1)
    Threader.incr_in_executor(1)
  }
  py.exec_async('import time; time.sleep(0.00001)').then(() => {
  }).catch(console.error)
  sleep(50).then(
    () => {
      Threader.__refresh__()
      assert(3 * times === Threader.counter)
      console.log('. testThreader OK!')
    })
    .catch(console.error)
}

function testMultiprocessor (): void {
  const py = new Python()
  py.add_syspath('plugins')
  const MultiProcessor = py.import('MultiProcessor')
  MultiProcessor.run()
  MultiProcessor.run_in_executor()
  const times = 100
  let done = 0
  for (let i = 0; i < times; i++) {
    MultiProcessor.run_in_executor_async()
      .then((data) => {
        assert(data < 0.0001)
        done += 1
        if (done === times) {
          console.log('. testMultiprocessor OK!')
        }
      })
      .catch((err) => {
        console.error(err)
      })
  }
}

function testRefresh (): void {
  const py = new Python()
  const sys = py.import('sys')
  const version: string = sys.version
  py.exec("import sys;sys.version = '3.8.6'")
  py.refresh(sys) // == sys.__refresh__()
  assert(sys.version === '3.8.6')
  py.exec(`import sys;sys.version = '''${version}'''`)
  console.log('. testRefresh OK!')
}

function testGc (): void {
  const py = new Python()
  const os = py.import('os')
  py.gc(os)
  assert(clib.objects[os.__wrapper__.value] === undefined)
  console.log('. testGc OK!')
}

function testClear (): void {
  const py = new Python()
  const os = py.import('os')
  py.clear()
  assert(clib.objects[os.__wrapper__.value] === undefined)
  console.log('. testClear OK!')
}

function testContext (): void {
  const py1 = new Python({ context: true })
  const py2 = new Python({ context: true })
  const os1 = py1.import('os')
  const os2 = py2.import('os')
  const inspect1 = py1.import('inspect')
  inspect1.getdoc(os1)
  assert.throws(() => {
    inspect1.getdoc(os2)
  }) // error!
  py1.exec('a=1')
  assert(py1.eval('a') === 1)
  py1.clear()
  py2.clear()
  console.log('. testContext OK!')
}

function test (): void {
  testClear()
  testGc()
  testRefresh()
  testDummy()
  testExecEval()
  testContext()
  testExcel()
  testAsync().catch((err) => console.error(err))
  testThreader()
  testMultiprocessor()
}

test()
