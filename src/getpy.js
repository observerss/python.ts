#!/usr/bin/env node
// 本脚本主要用于在Windows环境下面下载平台相关的Python文件
// 目前还有点问题，没找到好一点的方式来自动下载，所以先在README里面说明吧

var AdmZip = require('adm-zip')
const fs = require('fs')
const http = require('http')

if (process.platform === 'win32') {
  if (process.arch === 'x64')
  console.log('Fetching x64 packages...')
  const name = 'python3.8-windows-x64.zip'
  const path = `./${name}`
  const file = fs.createWriteStream(path)
  const request = http.get(`http://github.com/observerss/python.ts/releases/download/v0/${name}`, function(response) {
    response.pipe(file)
    response.on('close', () => {
      new AdmZip(path).extractAllTo(process.arch, true)
      fs.unlinkSync(path)
    })
  })
}