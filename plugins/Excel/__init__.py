import openpyxl


class Excel:
    def __init__(self):
        self.workbook = None

    def open_workbook(self, name):
        self.workbook = openpyxl.open(name)

    def read_cell(self, row: int, col: int):
        return self.workbook.active._cells[(row + 1, col + 1)].value

    def write_cell(self, row: int, col: int, value: object):
        self.workbook.active._cells[(row + 1, col + 1)].value = value
