local nmap = require("config.utils").nmap

nmap("<leader>ee", ":sp term://just build<cr>")
nmap("<leader>ef", ":sp term://just flash<cr>")
nmap("<leader>eF", ":sp term://just flash monitor<cr>")
nmap("<leader>er", ":sp term://just run<cr>")
nmap("<leader>em", ":sp term://just monitor<cr>")
