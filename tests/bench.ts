import { Python } from '../src/python'
import assert = require('assert')

function benchDummy (times: number): void {
  const py = new Python({})
  py.add_syspath('plugins')
  const Dummy = py.import('Dummy')
  const dm = Dummy.Dummy().unwrap()
  assert(dm.dummy() === 'dummy')

  const t1 = +new Date()
  for (let i = 0; i < times; i++) {
    dm.dummy()
  }
  const t2 = +new Date()
  console.log('dummy function call', times, 'times in', t2 - t1, 'milliseconds => qps =', (times * 1000 / (t2 - t1)))
}

function benchExel (times: number): void {
  const py = new Python({})
  py.add_syspath('plugins')
  let Excel
  // 没安装openpyxl, 这个部分不测了
  try {
    Excel = py.import('Excel')
  } catch {
    return
  }
  const xls = Excel.Excel().unwrap()

  xls.open_workbook('tests/test.xlsx')
  const t1 = +new Date()
  for (let i = 0; i < times; i++) {
    const cell = xls.read_cell(0, 1)
    xls.write_cell(0, 1, cell)
    xls.write_cell(0, 1, cell)
  }
  const t2 = +new Date()
  console.log('excel function call', times, 'times in', t2 - t1, 'milliseconds => qps =', (times * 1000 / (t2 - t1)))
}

async function benchAsync (times: number): Promise<void> {
  const py = new Python({})
  py.add_syspath('plugins')
  const Dummy = py.import('Dummy')
  const dm = Dummy.Dummy().unwrap()
  assert(await dm.dummy_async() === 'dummy')

  const t1 = +new Date()
  for (let i = 0; i < times; i++) {
    await dm.dummy_async()
  }
  const t2 = +new Date()
  console.log('async function call', times, 'times in', t2 - t1, 'milliseconds => qps =', (times * 1000 / (t2 - t1)))
}

function benchImport (times, module?: string): void {
  const py = new Python()
  module = module ?? 'os'
  py.import(module)

  const t1 = +new Date()
  for (let i = 0; i < times; i++) {
    py.import(module)
  }
  const t2 = +new Date()
  console.log('import function call', times, 'times in', t2 - t1, 'milliseconds => qps =', (times * 1000 / (t2 - t1)))
}

function main (): void {
  console.log('Benchmarking...')
  benchImport(1000, 'os')
  benchDummy(100000)
  benchExel(10000)
  benchAsync(10000).catch((err) => {
    console.error(err)
  })
}

main()
